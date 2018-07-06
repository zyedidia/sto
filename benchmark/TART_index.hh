#pragma once
#include "config.h"
#include "compiler.hh"

#include "Sto.hh"

#include "string.hh"
#include "TART.hh"
#include <type_traits>

#include <vector>
#include "VersionSelector.hh"

namespace bench {
template <typename K, typename V, typename DBParams>
class tart_index : public TObject {
public:
    typedef K key_type;
    typedef V value_type;

    typedef std::tuple<bool, bool, uintptr_t, const value_type> sel_return_type;
    typedef std::tuple<bool, bool>                              ins_return_type;
    typedef std::tuple<bool, bool>                              del_return_type;
    uint64_t key_gen_;
    // static __thread typename table_params::threadinfo_type *ti;
    
    tart_index() {
        art = TART();
        static_assert(std::is_base_of<std::string, K>::value, "key must be std::string");
        key_gen_ = 0;
        //static_assert(std::is_base_of<uintptr_t, V>::value, "value must be uintptr_t");
    }
    ~tart_index() {}

    bool range_scan(const key_type& begin, const key_type& end, Callback callback,
                    std::initializer_list<column_access_t> accesses, bool phantom_protection = true, int limit = -1) {
        return false;
    }

    static void thread_init() {
        // if (ti == nullptr)
        //     ti = threadinfo::make(threadinfo::TI_PROCESS, TThread::id());
        // Transaction::tinfo[TThread::id()].trans_start_callback = []() {
        //     ti->rcu_start();
        // };
        // Transaction::tinfo[TThread::id()].trans_end_callback = []() {
        //     ti->rcu_stop();
        // };
    }

    // DB operations
    sel_return_type select_row(const key_type& k) {
        auto ret = art.lookup(k);
        return sel_return_type(true, true, 0, ret);
    }
    ins_return_type insert_row(const key_type& k, value_type& v, bool overwrite = false) {
        (void)overwrite;
        art.insert(k, v);
        return ins_return_type(true, false);
    }
    void update_row(const key_type& k, value_type& v) {
        art.insert(k, v);
    }
    del_return_type delete_row(const key_type& k) {
        art.erase(k);
        return del_return_type(true, true);
    }

    value_type nontrans_get(const key_type& k) {
        return art.nonTransGet(k.str());
    }
    void nontrans_put(const key_type& k, const value_type& v) {
        art.nonTransPut(k.str(), (art::TVal) &v);
    }

    uint64_t gen_key() {
        return fetch_and_add(&key_gen_, 1);
    }

    bool lock(TransItem& item, Transaction& txn) {
        return art.lock(item, txn);
    }
    bool check(TransItem& item, Transaction& txn) {
        return art.check(item, txn);
    }
    void install(TransItem& item, Transaction& txn) {
        art.install(item, txn);
    }
    void unlock(TransItem& item) {
        art.unlock(item);
    }
private:
    TART art;
};
}
