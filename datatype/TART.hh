#pragma once
#include "config.h"
#include "compiler.hh"
#include <vector>
#include "Interface.hh"
#include "Transaction.hh"
#include "TWrapped.hh"
#include "simple_str.hh"
#include "print_value.hh"
#include "../ART/Tree.h"
#include "../ART/N.h"

class TART : public TObject {
public:
    typedef const char* TKey;
    typedef uintptr_t TVal;
    typedef ART_OLC::N Node;

    struct Element {
        TKey key;
        int len;
        TVal val;
        TVersion vers;
        bool poisoned;
    };

    static void loadKey(TID tid, Key &key) {
        Element* e = (Element*) tid;
        if (e->len > 8) {
            key.setKeyLen(e->len);
            key.set(e->key, e->len);
        } else {
            key.setKeyLen(e->len);
            memcpy(&key[0], &e->key, e->len);
            // reinterpret_cast<uint64_t*>(&key[0])[0] = __builtin_bswap64((uint64_t) e->key);
        }
    }

    static void make_key(TKey tkey, Key &key, int len) {
        if (len > 8) {
            key.setKeyLen(len);
            key.set(tkey, len);
        } else {
            key.setKeyLen(len);
            memcpy(&key[0], tkey, len);
            // reinterpret_cast<uint64_t*>(&key[0])[0] = __builtin_bswap64((uint64_t) tkey);
        }
    }

    // static void loadKey(TID tid, Key &key) {
    //     Element* e = (Element*) tid;
    //     key.set(e->key.c_str(), e->key.size() + 1);
    // }

    typedef typename std::conditional<true, TVersion, TNonopaqueVersion>::type Version_type;
    typedef typename std::conditional<true, TWrapped<TVal>, TNonopaqueWrapped<TVal>>::type wrapped_type;

    static constexpr TransItem::flags_type parent_bit = TransItem::user0_bit;
    static constexpr TransItem::flags_type deleted_bit = TransItem::user0_bit<<1;
    static constexpr TransItem::flags_type new_insert_bit = TransItem::user0_bit<<2;

    TART() {
        root_.access().setLoadKey(TART::loadKey);
    }

    TVal transGet(TKey k, int len) {
        Key key;
        make_key(k, key, len);
        auto r = root_.access().lookup(key);
        Element* e = (Element*) r.first;
        if (e) {
            auto item = Sto::item(this, e);
            if (item.has_write()) {
                return item.template write_value<TVal>();
            }
            e->vers.lock_exclusive();
            if (e->poisoned) {
                e->vers.unlock_exclusive();
                throw Transaction::Abort();
            }
            e->vers.unlock_exclusive();
            e->vers.observe_read(item);
            return e->val;
        }
        assert(r.second);
        auto item = Sto::item(this, r.second);
        item.add_flags(parent_bit);
        r.second->vers.observe_read(item);
        return 0;
    }

    TVal nonTransGet(TKey k, int len) {
        Key key;
        // key.set(k.c_str(), k.size()+1);
        make_key(k, key, len);
        auto r = root_.access().lookup(key);
        Element* e = (Element*) r.first;
        if (e) {
            return e->val;
        }
        return 0;
    }

    TVal lookup(TKey k, int len) {
        return transGet(k, len);
    }
    TVal lookup(uint64_t k) {
        return transGet((TKey) &k, sizeof(uint64_t));
    }
    TVal lookup(const char* k) {
        return transGet(k, strlen(k)+1);
    }

    void transPut(TKey k, TVal v, int len) {
        Key art_key;
        make_key(k, art_key, len);
        // art_key.set(k.c_str(), k.size()+1);
        auto r = root_.access().lookup(art_key);
        Element* e = (Element*) r.first;
        if (e) {
            auto item = Sto::item(this, e);
            e->vers.lock_exclusive();
            if (!item.has_write() && e->poisoned) {
                e->vers.unlock_exclusive();
                throw Transaction::Abort();
            }
            e->vers.unlock_exclusive();
            item.add_write(v);
            item.clear_flags(deleted_bit);
            return;
        }

        e = new Element();
        if (len > 8) {
            char* p = (char*) malloc(len);
            memcpy(p, k, len);
            e->key = p;
        } else {
            memcpy(&e->key, k, len);
        }
        e->val = v;
        e->poisoned = true;
        e->len = len;
        bool success;
        root_.access().insert(art_key, (TID) e, &success);
        if (!success) {
            free(e);
            throw Transaction::Abort();
        }
        auto item_el = Sto::item(this, e);
        auto item_parent = Sto::item(this, r.second);
        item_parent.add_write(v);
        item_el.add_write(v);
        item_el.add_flags(new_insert_bit);
        item_parent.add_flags(parent_bit);
    }

    void nonTransPut(TKey k, TVal v, int len) {
        Key art_key;
        // art_key.set(k.c_str(), k.size()+1);
        make_key(k, art_key, len);
        auto r = root_.access().lookup(art_key);
        Element* e = (Element*) r.first;
        if (e) {
            e->val = v;
            return;
        }

        e = new Element();
        if (len > 8) {
            char* p = (char*) malloc(len);
            memcpy(p, k, len);
            e->key = p;
        } else {
            memcpy(&e->key, k, len);
        }
        e->val = v;
        e->len = len;
        root_.access().insert(art_key, (TID) e, nullptr);
    }

    void insert(TKey k, TVal v, int len) {
        transPut(k, v, len);
    }
    void insert(uint64_t k, TVal v) {
        transPut((TKey) &k, v, sizeof(uint64_t));
    }
    void insert(const char* k, TVal v) {
        transPut(k, v, strlen(k)+1);
    }

    void transRemove(TKey k, int len) {
        Key art_key;
        // art_key.set(k.c_str(), k.size()+1);
        make_key(k, art_key, len);
        auto r = root_.access().lookup(art_key);
        Element* e = (Element*) r.first;
        if (e) {
            auto item = Sto::item(this, e);
            e->vers.lock_exclusive();
            if (!item.has_write() && e->poisoned) {
                e->vers.unlock_exclusive();
                throw Transaction::Abort();
            }
            e->poisoned = true;
            e->vers.unlock_exclusive();
            item.add_write(0);
            item.add_flags(deleted_bit);

            // auto item_parent = Sto::item(this, r.second);
            // item_parent.add_write(0);
            // item_parent.add_flags(parent_bit);
            return;
        }

        auto item_parent = Sto::item(this, r.second);
        item_parent.add_flags(parent_bit);
        r.second->vers.observe_read(item_parent);
    }

    void erase(TKey k, int len) {
        transRemove(k, len);
    }
    void erase(uint64_t k) {
        transRemove((TKey) &k, sizeof(uint64_t));
    }
    void erase(const char* k) {
        transRemove(k, strlen(k)+1);
    }

    bool lock(TransItem& item, Transaction& txn) override {
        if (item.has_flag(parent_bit)) {
            Node* parent = item.template key<Node*>();
            return parent->vers.is_locked_here() || txn.try_lock(item, parent->vers);
        } else {
            Element* e = item.template key<Element*>();
            bool locked = txn.try_lock(item, e->vers);
            if (!locked) {
                return false;
            }
            if (e->poisoned && !(item.has_flag(new_insert_bit) || item.has_flag(deleted_bit))) {
                e->vers.cp_unlock(item);
                return false;
            }
            return true;
        }
    }
    bool check(TransItem& item, Transaction& txn) override {
        if (item.has_flag(parent_bit)) {
            Node* parent = item.template key<Node*>();
            return parent->vers.cp_check_version(txn, item);
        } else {
            Element* e = item.template key<Element*>();
            return !e->poisoned && e->vers.cp_check_version(txn, item);
        }
    }
    void install(TransItem& item, Transaction& txn) override {
        if (item.has_flag(parent_bit)) {
            Node* parent = item.template key<Node*>();
            txn.set_version_unlock(parent->vers, item);
        } else {
            Element* e = item.template key<Element*>();
            if (item.has_flag(deleted_bit)) {
                Key art_key;
                // art_key.set(e->key.c_str(), e->key.size()+1);
                if (e->len > 8) {
                    make_key(e->key, art_key, e->len);
                } else {
                    make_key((const char*) &e->key, art_key, e->len);
                }
                root_.access().remove(art_key, (TID) e);
                Transaction::rcu_delete(e);
                return;
            }
            e->poisoned = false;
            e->val = item.template write_value<TVal>();
            txn.set_version_unlock(e->vers, item);
        }
    }
    void unlock(TransItem& item) override {
        if (item.has_flag(parent_bit)) {
            Node* parent = item.template key<Node*>();
            if (parent->vers.is_locked_here()) parent->vers.cp_unlock(item);
        } else {
            Element* e = item.template key<Element*>();
            e->vers.cp_unlock(item);
        }
    }
    void cleanup(TransItem& item, bool committed) override {
        if (committed || item.has_flag(parent_bit)) {
            return;
        }
        Element* e = item.template key<Element*>();
        if (e->poisoned) {
            Key art_key;
            // art_key.set(e->key.c_str(), e->key.size()+1);
            if (e->len > 8) {
                make_key(e->key, art_key, e->len);
            } else {
                make_key((const char*) &e->key, art_key, e->len);
            }
            root_.access().remove(art_key, (TID) e);
            Transaction::rcu_delete(e);
        }
    }
    void print(std::ostream& w, const TransItem& item) const override {
        root_.access().print();
    }
    
    void print() const {
        root_.access().print();
    }
protected:
    TOpaqueWrapped<ART_OLC::Tree> root_;
};
