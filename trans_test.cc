#include <string>
#include <iostream>
#include <assert.h>
#include <vector>
#include <random>
#include <map>
#include "Transaction.hh"
#include "Testers.hh"

#define GLOBAL_SEED 10
#define MAX_VALUE  100000 // Max value of integers used in data structures
#define NTRANS 10000 // Number of transactions each thread should run.
#define N_THREADS 4 // Number of concurrent threads
#define MAX_OPS 3 // Maximum number of operations in a transaction.
#define PRINT_DEBUG 0 // Set this to 1 to print some debugging statements.

#define PRIORITY_QUEUE 0
#define HASHTABLE 1
#define RBTREE 2
#define DS RBTREE

#if DS == PRIORITY_QUEUE
PqueueTester<PriorityQueue<int>> tester = PqueueTester<PriorityQueue<int>>();
#elif DS == HASHTABLE
HashtableTester<Hashtable<int, int, false, 1000000>> tester = HashtableTester<Hashtable<int, int, false, 1000000>>();
#elif DS == RBTREE
RBTreeTester<RBTree<int, int>> tester = RBTreeTester<RBTree<int, int>>();
#endif

template <typename T>
void run(T* q, int me) {
    Transaction::threadid = me;
    
    std::uniform_int_distribution<long> slotdist(0, MAX_VALUE);
    for (int i = 0; i < NTRANS; ++i) {
        // so that retries of this transaction do the same thing
        auto transseed = i;
        txn_record *tr = new txn_record;
        TRANSACTION {
            tr->ops.clear();
            
            uint32_t seed = transseed*3 + (uint32_t)me*NTRANS*7 + (uint32_t)GLOBAL_SEED*MAX_THREADS*NTRANS*11;
            auto seedlow = seed & 0xffff;
            auto seedhigh = seed >> 16;
            Rand transgen(seed, seedlow << 16 | seedhigh);
            
            int numOps = slotdist(transgen) % MAX_OPS + 1;
            
            for (int j = 0; j < numOps; j++) {
                int op = slotdist(transgen) % tester.num_ops_;
                tr->ops.push_back(tester.doOp(q, op, me, slotdist, transgen));
            }
        }
        if (Sto::try_commit()) {
#if PRINT_DEBUG
            TransactionTid::lock(lock);
            std::cout << "[" << me << "] committed " << Sto::commit_tid() << std::endl;
            TransactionTid::unlock(lock);
#endif
            txn_list[me][Sto::commit_tid()] = tr;
            break;
        } else {
#if PRINT_DEBUG
            TransactionTid::lock(lock); std::cout << "[" << me << "] aborted "<< std::endl; TransactionTid::unlock(lock);
#endif
        }
        
    } catch (Transaction::Abort e) {
#if PRINT_DEBUG
        TransactionTid::lock(lock); std::cout << "[" << me << "] aborted "<< std::endl; TransactionTid::unlock(lock);
#endif
    }
    }
    }
}

template <typename T>
void* runFunc(void* x) {
    TesterPair<T>* tp = (TesterPair<T>*) x;
    run(tp->t, tp->me);
    return nullptr;
}


template <typename T>
void startAndWait(T* queue) {
    pthread_t tids[N_THREADS];
    TesterPair<T> testers[N_THREADS];
    for (int i = 0; i < N_THREADS; ++i) {
        testers[i].t = queue;
        testers[i].me = i;
        pthread_create(&tids[i], NULL, runFunc<T>, &testers[i]);
    }
    pthread_t advancer;
    pthread_create(&advancer, NULL, Transaction::epoch_advancer, NULL);
    pthread_detach(advancer);
    
    for (int i = 0; i < N_THREADS; ++i) {
        pthread_join(tids[i], NULL);
    }
}

void print_time(struct timeval tv1, struct timeval tv2) {
    printf("%f\n", (tv2.tv_sec-tv1.tv_sec) + (tv2.tv_usec-tv1.tv_usec)/1000000.0);
}

int main() {
    lock = 0;

#if DS == PRIORITY_QUEUE 
    PriorityQueue<int> q;
    PriorityQueue<int> q1;
#elif DS == HASHTABLE
    Hashtable<int, int, false, 1000000> q;
    Hashtable<int, int, false, 1000000> q1;
#elif DS == RBTREE
    RBTree<int, int> q;
    RBTree<int, int> q1;
#endif  

    tester.init(&q);
    tester.init(&q1);

    struct timeval tv1,tv2;
    gettimeofday(&tv1, NULL);
    
    for (int i = 0; i < N_THREADS; i++) {
        txn_list.emplace_back();
    }
    
    startAndWait(&q);
    
    gettimeofday(&tv2, NULL);
    printf("Parallel time: ");
    print_time(tv1, tv2);
    
#if PERF_LOGGING
    Transaction::print_stats();
    {
        using thd = threadinfo_t;
        thd tc = Transaction::tinfo_combined();
        printf("total_n: %llu, total_r: %llu, total_w: %llu, total_searched: %llu, total_aborts: %llu (%llu aborts at commit time)\n", tc.p(txp_total_n), tc.p(txp_total_r), tc.p(txp_total_w), tc.p(txp_total_searched), tc.p(txp_total_aborts), tc.p(txp_commit_time_aborts));
    }
#endif
    
    
    std::map<uint64_t, txn_record *> combined_txn_list;
    
    for (int i = 0; i < N_THREADS; i++) {
        combined_txn_list.insert(txn_list[i].begin(), txn_list[i].end());
    }
    
    std::cout << "Single thread replay" << std::endl;
    gettimeofday(&tv1, NULL);
    
    std::map<uint64_t, txn_record *>::iterator it = combined_txn_list.begin();
    for(; it != combined_txn_list.end(); it++) {
        Sto::start_transaction();
        for (unsigned i = 0; i < it->second->ops.size(); i++) {
            tester.redoOp(&q1, it->second->ops[i]);
        }
        assert(Sto::try_commit());
    }
    
    gettimeofday(&tv2, NULL);
    printf("Serial time: ");
    print_time(tv1, tv2);
    
   
    tester.check(&q, &q1);
	return 0;
}