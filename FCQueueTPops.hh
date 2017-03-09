#ifndef FCQUEUETPOPS_H 
#define FCQUEUETPOPS_H
//#define DEBUGQ

#include <deque>
#include <list>
#include <math.h>
#include "FlatCombining.hh"
#include "Transaction.hh"
#include "cpp_framework.hh"

using namespace CCP;

/*
 * This transactional flat combining queue pops at execution time, acquiring a lock on the queue until the transaction finishes.
 */

template <typename T>
class FCQueueTPops : public Shared {

private:

	static const int _MAX_THREADS	= 1024;
	static const int _NULL_VALUE	= 0;
	static const int _ABORT_VALUE   = (INT_MIN+1); // should abort!
	static const int _CLEANUP_VALUE = (INT_MIN+2); 
	static const int _UNLOCK_VALUE  = (INT_MIN+3); 
	static const int _POP_VALUE     = (INT_MIN+4); 
	static const int _PUSH_VALUE    = (INT_MIN+5); 
	const int		_NUM_THREADS    = 20;

	//list inner types ------------------------------
	struct SlotInfo {
		int volatile		_req_ans;		//here 1 can post the request and wait for answer
		void* volatile		_req_list;		//here 1 can post the request and wait for answer
		int volatile		_tid;	        //which thread is making the request
		SlotInfo* volatile	_next;			//when NULL not connected

		SlotInfo() {
			_req_ans	 = _NULL_VALUE;
            _req_list    = NULL;
			_next		 = NULL;
		}
	};

	//list fields -----------------------------------
	static thread_local SlotInfo*   _tls_slot_info;
    AtomicReference<SlotInfo>       _head_slot;
	int volatile					_timestamp;

	//list helper function --------------------------
	SlotInfo* get_new_slot() {
		SlotInfo* my_slot= new SlotInfo();
		_tls_slot_info = my_slot;

		SlotInfo* curr_head;
		do {
			curr_head = _head_slot.get();
			my_slot->_next = curr_head;
		} while(false == _head_slot.compareAndSet(curr_head, my_slot));

		return my_slot;
	}

	void enq_slot(SlotInfo* p_slot) {
		SlotInfo* curr_head;
		do {
			curr_head = _head_slot.get();
			p_slot->_next = curr_head;
		} while(false == _head_slot.compareAndSet(curr_head, p_slot));
	}

	void enq_slot_if_needed(SlotInfo* p_slot) {
		if(NULL == p_slot->_next) {
			enq_slot(p_slot);
		}
	}

	struct Node {
		Node* volatile	_next;
		int volatile	_values[256];

		static Node* get_new(const int in_num_values) {
			const size_t new_size = (sizeof(Node) + (in_num_values + 2 - 256) * sizeof(int));

			Node* const new_node = (Node*) calloc(1,new_size);
			new_node->_next = NULL;
			return new_node;
		}
	};

    AtomicInteger   _fc_lock;
	char			_pad1[CACHE_LINE_SIZE];
	const int		_NUM_REP;
	const int		_REP_THRESHOLD;
	Node* volatile	_tail;
	Node* volatile	_head;
    int volatile    _locked_tid;
	int volatile	_NODE_SIZE;
	Node* volatile	_new_tail_node;
	Node* volatile	_repushed_vals_node;

	inline void flat_combining() {
		// prepare for enq
		int volatile* enq_value_ary;
		int volatile* repushed_value_ary;
		if(NULL == _new_tail_node) 
			_new_tail_node = Node::get_new(_NODE_SIZE);
		if(NULL == _repushed_vals_node) 
			_repushed_vals_node = Node::get_new(_NODE_SIZE);
		enq_value_ary = _new_tail_node->_values;
        *enq_value_ary = 1;
		++enq_value_ary;
		
		repushed_value_ary = _repushed_vals_node->_values;
        *repushed_value_ary = 1;
		++repushed_value_ary;

		// prepare for deq
		int volatile * deq_value_ary = _head->_values;
		deq_value_ary += *deq_value_ary;

		int num_added = 0;
		int num_repushed = 0;
		for (int iTry=0; iTry<_NUM_REP; ++iTry) {
			Memory::read_barrier();

			int num_changes=0;
			SlotInfo* curr_slot = _head_slot.get();
			while(NULL != curr_slot->_next) {
                int tid = curr_slot->_tid;

                // check if the queue is "locked" and if this thread is the 
                // locking thread. abort if the queue is indeed locked.
                if (_locked_tid >= 0 && tid != _locked_tid) {
                    curr_slot->_req_ans = _ABORT_VALUE;
                    curr_slot = curr_slot->_next;
                    continue;
                }

				const int curr_value = curr_slot->_req_ans;

                // PUSHES
                // we want to push a value
                // done when sets curr_value to NULL
				if(curr_value > _NULL_VALUE) {
					++num_changes;
					*enq_value_ary = curr_value;
					++enq_value_ary;
					curr_slot->_req_ans = _NULL_VALUE;

					++num_added;
					if(num_added >= _NODE_SIZE) {
						Node* const new_tail_node2 = Node::get_new(_NODE_SIZE+4);
						memcpy((void*)(new_tail_node2->_values), (void*)(_new_tail_node->_values), (_NODE_SIZE+2)*sizeof(int) );
						_new_tail_node = new_tail_node2; 
						enq_value_ary = _new_tail_node->_values;
						*enq_value_ary = 1;
						++enq_value_ary;
						enq_value_ary += _NODE_SIZE;
						_NODE_SIZE += 4;
					}

                // CLEANUP repushes the nodes on the front of the queue.
                } else if(_CLEANUP_VALUE == curr_value) {
                    auto popped_list = (std::list<int>*) curr_slot->_req_list;
                    while(!popped_list->empty()) {
                        const int value = popped_list->front();
                        ++num_changes;
                        *repushed_value_ary = value;
                        ++repushed_value_ary;

                        num_repushed++;
                        if(num_repushed >= _NODE_SIZE) {
                            assert(0);
                            Node* const repushed_vals_node2 = Node::get_new(_NODE_SIZE+4);
                            memcpy((void*)(repushed_vals_node2->_values), (void*)(_repushed_vals_node->_values), (_NODE_SIZE+2)*sizeof(int) );
                            _repushed_vals_node = repushed_vals_node2; 
                            repushed_value_ary = _repushed_vals_node->_values;
                            *repushed_value_ary = (1);
                            ++repushed_value_ary;
                            repushed_value_ary += _NODE_SIZE;
                            _NODE_SIZE += 4;
                        }
                        popped_list->pop_front();
                    }
                    curr_slot->_req_list = NULL;

                // POPS 
                // done when sets curr_value to some negative value (found), NULL (empty), or ABORT
                // this pops an item off the queue and locks the queue, or aborts if the queue is locked
                // by someone else
				} else if(_POP_VALUE == curr_value) {
					++num_changes;
                    _locked_tid = tid;
                    const int curr_deq = *deq_value_ary;
                    if(0 != curr_deq) {
                        curr_slot->_req_ans = curr_deq;
                        ++deq_value_ary;
                    } else if(NULL != _head->_next) {
                        auto tmp = _head;
                        _head = _head->_next;
                        free(tmp);
                        deq_value_ary = _head->_values;
                        deq_value_ary += *deq_value_ary;
                        continue;
                    } else {
                        curr_slot->_req_ans = _NULL_VALUE;
                    }

                // UNLOCK by setting the locked_tid to -1.
                } else if(_UNLOCK_VALUE == curr_value) {
                    _locked_tid = -1;
                    curr_slot->_req_ans = _NULL_VALUE;
                }
				curr_slot = curr_slot->_next;
			}//while on slots

			if(num_changes < _REP_THRESHOLD)
				break;
		}//for repetition

		if(0 == *deq_value_ary && NULL != _head->_next) {
            auto tmp = _head;
			_head = _head->_next;
            free(tmp);
		} else {
            // set where to start next in dequeing
			*_head->_values = (deq_value_ary -  _head->_values);
		}

		if(repushed_value_ary != (_repushed_vals_node->_values + 1)) {
			*repushed_value_ary = (0);
			_repushed_vals_node->_next = _head;
			_head = _repushed_vals_node;
			_repushed_vals_node = NULL;
		} 
		if(enq_value_ary != (_new_tail_node->_values + 1)) {
			*enq_value_ary = (0);
			_tail->_next = _new_tail_node;
			_tail = _new_tail_node;
			_new_tail_node  = NULL;
		} 
	}

public:
    typedef T           value_type;     ///< Value type

private:
    // STO
    static constexpr TransItem::flags_type empty_q_bit = TransItem::user0_bit<<1;
    static constexpr TransItem::flags_type list_bit = TransItem::user0_bit<<2;
    static constexpr int pushitem_key = -1;
    static constexpr int popitem_key = -2;

public:
	FCQueueTPops() :	_NUM_REP(_NUM_THREADS), _REP_THRESHOLD((int)(ceil(_NUM_THREADS/(1.7))))
	{
		_tail = Node::get_new(_NUM_THREADS);
		_head = _tail;
		_tail->_values[0] = (1);
		_tail->_values[1] = (0);

		_head_slot.set(new SlotInfo());
		_timestamp = 0;
		_NODE_SIZE = 4;
		_new_tail_node = NULL;
		_repushed_vals_node = NULL;
        _locked_tid = -1;
	}

	virtual ~FCQueueTPops() { }

    // TRANSACTIONAL CALLS
    void push(const T& v) {
        auto item = Sto::item(this, pushitem_key);
        if (item.has_write()) {
            if (!is_list(item)) {
                auto& val = item.template write_value<T>();
                std::list<T> write_list;
                write_list.push_back(val);
                write_list.push_back(v);
                item.clear_write();
                item.add_write(write_list);
                item.add_flags(list_bit);
            }
            else {
                auto& write_list = item.template write_value<std::list<T>>();
                write_list.push_back(v);
            }
        }
        else item.add_write(v);
    }

	bool pop() {
        int popped = fc_pop(); 
        std::list<T> popped_list;
        auto popitem = Sto::item(this, popitem_key);
        if (!popitem.has_write()) {
            if (popped) {
                popped_list.push_back(popped);
            }
            popitem.add_write(popped_list);
        } else {
            auto& popped_list = popitem.template write_value<std::list<T>>();
            if (popped) {
                popped_list.push_back(popped);
            }
        }
        return popped;
	}

	bool fc_push_val(const int inValue) {
		SlotInfo* my_slot = _tls_slot_info;
		if(NULL == my_slot)
			my_slot = get_new_slot();

		SlotInfo* volatile&	my_next = my_slot->_next;
		int volatile& my_re_ans = my_slot->_req_ans;
		int volatile& my_re_tid = my_slot->_tid;
		my_re_tid = TThread::id();
		my_re_ans = inValue;

		do {
			if (NULL == my_next)
				enq_slot(my_slot);

			bool is_cas = true;
			if(lock_fc(_fc_lock, is_cas)) {
				flat_combining();
				_fc_lock.set(0);
				return true;
			} else {
				Memory::write_barrier();
				if(!is_cas)
				while(_NULL_VALUE != my_re_ans && 0 != _fc_lock.getNotSafe()) {
                    sched_yield();
				} 
				Memory::read_barrier();
				if(_NULL_VALUE == my_re_ans) {
					return true;
				}
			}
		} while(true);
	}

	bool fc_push(std::list<int>* write_list) {
		SlotInfo* my_slot = _tls_slot_info;
		if(NULL == my_slot)
			my_slot = get_new_slot();

		SlotInfo* volatile&	my_next = my_slot->_next;
        void* volatile& my_re_list = my_slot->_req_list;
		int volatile& my_re_ans = my_slot->_req_ans;
		int volatile& my_re_tid = my_slot->_tid;
		my_re_tid = TThread::id();
		my_re_list = write_list;
        my_re_ans = _PUSH_VALUE;

		do {
			if (NULL == my_next)
				enq_slot(my_slot);

			bool is_cas = true;
			if(lock_fc(_fc_lock, is_cas)) {
				flat_combining();
				_fc_lock.set(0);
				return true;
			} else {
				Memory::write_barrier();
				if(!is_cas)
				while(NULL != my_re_list && 0 != _fc_lock.getNotSafe()) {
                    sched_yield();
				} 
				Memory::read_barrier();
				if(NULL == my_re_list) {
					return true;
				}
			}
		} while(true);
	}

	void fc_unlock() {
		SlotInfo* my_slot = _tls_slot_info;
		if(NULL == my_slot)
			my_slot = get_new_slot();

		SlotInfo* volatile&	my_next = my_slot->_next;
		int volatile& my_re_ans = my_slot->_req_ans;
		int volatile& my_re_tid = my_slot->_tid;
		my_re_tid = TThread::id();
		my_re_ans = _UNLOCK_VALUE;

		do {
			if (NULL == my_next)
				enq_slot(my_slot);

			bool is_cas = true;
			if(lock_fc(_fc_lock, is_cas)) {
				flat_combining();
				_fc_lock.set(0);
                break;
			} else {
				Memory::write_barrier();
				if(!is_cas)
				while(_NULL_VALUE != my_re_ans && 0 != _fc_lock.getNotSafe()) {
                    sched_yield();
				} 
				Memory::read_barrier();
				if(_NULL_VALUE == my_re_ans) {
					return;
				}
			}
		} while(true);
	}

    int fc_pop() {
        SlotInfo* my_slot = _tls_slot_info;
		if(NULL == my_slot)
			my_slot = get_new_slot();

		SlotInfo* volatile&	my_next = my_slot->_next;
		int volatile& my_re_ans = my_slot->_req_ans;
		int volatile& my_re_tid = my_slot->_tid;
		my_re_tid = TThread::id();
		my_re_ans = _POP_VALUE;

		do {
			if (NULL == my_next)
				enq_slot(my_slot);

			bool is_cas = true;
			if(lock_fc(_fc_lock, is_cas)) {
				flat_combining();
				_fc_lock.set(0);
                if (my_re_ans == _ABORT_VALUE)
                    Sto::abort();
                break;
			} else {
				Memory::write_barrier();
				if(!is_cas)
				while(_POP_VALUE == my_re_ans && 0 != _fc_lock.getNotSafe()) {
                    sched_yield();
				}
				Memory::read_barrier();
				if(_POP_VALUE != my_re_ans) {
                    if (my_re_ans == _ABORT_VALUE)
                        Sto::abort();
                    break;
				}
			}
		} while(true);
        return my_re_ans;
    }

    void fc_cleanup(std::list<int>* popped_list) {
		SlotInfo* my_slot = _tls_slot_info;
		if(NULL == my_slot)
			my_slot = get_new_slot();

		SlotInfo* volatile&	my_next = my_slot->_next;
        void* volatile& my_re_list = my_slot->_req_list;
		int volatile& my_re_tid = my_slot->_tid;
		int volatile& my_re_ans = my_slot->_req_ans;
		my_re_tid = TThread::id();
		my_re_list = popped_list;
		my_re_ans = _CLEANUP_VALUE;

		do {
			if (NULL == my_next)
				enq_slot(my_slot);

			bool is_cas = true;
			if(lock_fc(_fc_lock, is_cas)) {
				flat_combining();
				_fc_lock.set(0);
                return;
			} else {
				Memory::write_barrier();
				if(!is_cas)
				while(NULL != my_re_list && 0 != _fc_lock.getNotSafe()) {
                    sched_yield();
				} 
				Memory::read_barrier();
				if(NULL == my_re_list) {
                    return;
				}
			}
		} while(true);
    }

private:
    bool is_list(const TransItem& item) {
        return item.flags() & list_bit;
    }
 
    bool lock(TransItem&, Transaction&) override {
        return true;
    }

    bool check(TransItem&, Transaction&) override {
        return true;
    }

    void install(TransItem& item, Transaction&) override {
        // install pushes
        if (item.key<int>() == pushitem_key) {
            // write all the elements
            if (is_list(item)) {
                auto& write_list = item.template write_value<std::list<T>>();
                while(!write_list.empty()) {
                    //XXX
                    fc_push_val(write_list.front());
                    write_list.pop_front();
                }
                //fc_push(write_list);
            } else {
                auto& val = item.template write_value<T>();
                fc_push_val(val);
            }
        }
    }
    
    void unlock(TransItem& item) override {
        if (item.key<int>() == popitem_key) {
            fc_unlock();
        }
    }

    void cleanup(TransItem& item, bool committed) override {
        (void)item;
        if (!committed && item.key<int>() == popitem_key) {
            auto& popped_list = item.template write_value<std::list<T>>();
            fc_cleanup(&popped_list);
            fc_unlock();
        }
    }
};

template <typename T>
thread_local typename FCQueueTPops<T>::SlotInfo* FCQueueTPops<T>::_tls_slot_info = NULL;

#endif // #ifndef FCQUEUETPOPS_H