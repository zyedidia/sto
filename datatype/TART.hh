#pragma once
#include "config.h"
#include "compiler.hh"
#include <vector>
#include "Interface.hh"
#include "Transaction.hh"
#include "TWrapped.hh"
#include "simple_str.hh"
#include "print_value.hh"
#include "ART.hh"

static const unsigned char* c_str(std::string s) {
    return (const unsigned char*) s.c_str();
}

class TART : public TObject {
public:
    typedef std::string Key;
    typedef std::string key_type;
    typedef uintptr_t Value;
    typedef uintptr_t Value_type;

    typedef typename std::conditional<true, TVersion, TNonopaqueVersion>::type Version_type;
    typedef typename std::conditional<true, TWrapped<Value>, TNonopaqueWrapped<Value>>::type wrapped_type;

    static constexpr TransItem::flags_type deleted_bit = TransItem::user0_bit;

    TART() {
        art_tree_init(&root_.access());
    }

    std::pair<bool, Value> transGet(Key k) {
        auto item = Sto::item(this, k);
        if (item.has_write()) {
            auto val = item.template write_value<Value>();
            return {true, val};
        } else {
            std::pair<bool, Value> ret;
            vers_.lock_exclusive();
            // printf("art searching ... ... ... ");
            art_leaf* leaf = art_search(&root_.access(), c_str(k), k.length());
            // printf("art searched\n");
            Value val = 0;
            if (leaf != NULL) {
                val = (Value) leaf->value;
                leaf->vers.observe_read(item);
                // printf("GET observed key %s, val is %d, vers is %d\n", c_str(k), val, leaf->vers);
            } else {
                vers_.observe_read(item);
            }
            vers_.unlock_exclusive();
            ret = {true, val};
            // printf("get key: %s, val: %lu\n", k.c_str(), val);
            return ret;
        }
    }

    Value lookup(Key k) {
        auto result = transGet(k);
        if (!result.first) {
            throw Transaction::Abort();
        } else {
            return result.second;
        }
    }

    void transPut(Key k, Value v) {
        // printf("put key: %s, val: %lu\n", k.c_str(), v);
        auto item = Sto::item(this, k);
        item.add_write(v);
        item.clear_flags(deleted_bit);
    }

    void insert(Key k, Value v) {
        transPut(k, v);
    }

    void erase(Key k) {
        // printf("erase key: %s\n", k.c_str());
        auto item = Sto::item(this, k);
        item.add_flags(deleted_bit);
        item.add_write(0);
    }

    Value nonTransGet(Key k) {
        return art_search(&root_.access(), c_str(k), k.length());
    }
    void nonTransPut(Key k, Value v) {
        art_insert(&root_.access(), c_str(k), k.length(), (void*) v, nullptr);
    }

    bool lock(TransItem& item, Transaction& txn) override {
        return vers_.is_locked_here() || txn.try_lock(item, vers_);
    }
    bool check(TransItem& item, Transaction& txn) override {
        Key key = item.template key<Key>();
        // printf("checking %s\n", c_str(key));
        art_leaf* s = art_search(&root_.access(), c_str(key), key.length());
        if (s == NULL) {
            // printf("null check, tree version is %d\n", vers_);
            return vers_.cp_check_version(txn, item);
        }
        // Value v = (Value) s->value;
        // bool new_insert = false;
        // s = art_insert(&root_.access(), c_str(key), key.length(), (void*) v, &new_insert);
        // printf("check returning, version is %d\n", s->vers);
        return s->vers.cp_check_version(txn, item);
    }
    void install(TransItem& item, Transaction& txn) override {
        Value val = item.template write_value<Value>();
        Key key = item.template key<Key>();

        if (item.has_flag(deleted_bit)) {
            art_delete(&root_.access(), c_str(key), key.length());
            txn.set_version(vers_);
        } else {
            bool new_insert = false;
            art_leaf* s = art_insert(&root_.access(), c_str(key), key.length(), (void*) val, &new_insert);
            if (new_insert) {
                // printf("tree version set\n");
                txn.set_version(vers_);
            }
            s->vers.lock_exclusive();
            // printf("install: key is %s, val version was %d\n", c_str(key), s->vers);
            txn.set_version(s->vers);
            // printf("install: val version set to %d\n", s->vers);
            s->vers.unlock_exclusive();
        }

        // printf("install key: %s, val: %lu\n", key.c_str(), val);
    }
    void unlock(TransItem& item) override {
        if (vers_.is_locked_here()) {
            vers_.cp_unlock(item);
        }
        // printf("UNLOCKED\n");
    }
    void print(std::ostream& w, const TransItem& item) const override {
        w << "{TART<" << typeid(int).name() << "> " << (void*) this;
        if (item.has_read())
            w << " R" << item.read_value<Version_type>();
        if (item.has_write())
            w << " =" << item.write_value<int>();
        w << "}";
    }
protected:
    Version_type vers_;
    TOpaqueWrapped<art_tree> root_;
};
