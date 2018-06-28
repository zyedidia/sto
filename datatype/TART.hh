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
    typedef std::string TKey;
    typedef uintptr_t TVal;
    typedef std::pair<TID, ART_OLC::N*> item_val;

    struct Element {
        TKey key;
        TVal val;
        TVersion vers;
    };

    static void loadKey(TID tid, Key &key) {
        Element* e = (Element*) tid;
        key.set(e->key.c_str(), e->key.size());
    }

    typedef typename std::conditional<true, TVersion, TNonopaqueVersion>::type Version_type;
    typedef typename std::conditional<true, TWrapped<TVal>, TNonopaqueWrapped<TVal>>::type wrapped_type;

    // static constexpr TransItem::flags_type absent_bit = TransItem::user0_bit;
    // static constexpr TransItem::flags_type newleaf_bit = TransItem::user0_bit<<1;
    // static constexpr TransItem::flags_type deleted_bit = TransItem::user0_bit<<2;

    TART() {
        root_.access().setLoadKey(TART::loadKey);
    }

    TVal transGet(TKey k) {
        Key key;
        key.set(k.c_str(), k.size());
        auto r = root_.access().lookup(key);
        if ((Element*) r.first) {
            Element* e = (Element*) r.first;
            e->vers.observe_read(item);
            return e->val;
        } else {
            ART_OLC::N* n = r.second;
            if (n) n->vers.observe_read(item);
            item.add_flags(absent_bit);
            return 0;
        }
    }

    TVal lookup(TKey k) {
        return transGet(k);
    }

    void transPut(TKey k, TVal v) {
        auto item = Sto::item(this, k);
        if (item.has_write()) {
            item_val i = item.template write_value<item_val>();
            Element* e = (Element*) i.first;
            e->key = k;
            e->val = v;
        } else {
            Element* e = new Element();
            e->key = k;
            e->val = v;
            item_val r = {(TID) e, nullptr};
            item.add_write<item_val>(r);
        }
        // item.clear_flags(deleted_bit);
    }

    void insert(TKey k, TVal v) {
        transPut(k, v);
    }

    void erase(TKey k) {
        auto item = Sto::item(this, k);
        if (item.has_write()) {
            item_val i = item.template write_value<item_val>();
            Element* e = (Element*) i.first;
            e->key = k;
            e->val = 0;
        } else {
            Element* e = new Element();
            e->key = k;
            e->val = 0;
            item_val r = {(TID) e, nullptr};
            item.add_write<item_val>(r);
        }
        // item.add_flags(deleted_bit);
    }

    bool lock(TransItem& item, Transaction& txn) override {
        printf("lock\n");
        item_val r;
        if (item.has_write()) {
            r = item.template write_value<item_val>();
        } else {
            r = item.template read_value<item_val>();
        }
        Element* e = (Element*) r.first;
        if (e == nullptr) {
            return txn.try_lock(item, r.second->vers);
        } else {
            return txn.try_lock(item, e->vers);
        }
    }
    bool check(TransItem& item, Transaction& txn) override {
        printf("check\n");
        item_val r;
        if (item.has_write()) {
            r = item.template write_value<item_val>();
        } else {
            r = item.template read_value<item_val>();
        }
        if (item.has_flag(absent_bit)) {
            // written items are not checked
            // if an item was read w.o absent bit and is no longer found, abort
            return r.second->vers.cp_check_version(txn, item);
        }
        Element* e = (Element*) r.first;
        // if an item w/ absent bit and is found, abort
        return e->vers.cp_check_version(txn, item);
    }
    void install(TransItem& item, Transaction& txn) override {
        printf("install\n");
        item_val r = item.template write_value<item_val>();

        // if (item.has_flag(deleted_bit)) {
        //     art_delete(&root_.access(), c_str(key), key.length());
        //     txn.set_version(vers_);
        // } else {
        if (r.first) {
            Element* e = (Element*) r.first;
            Key art_key;
            art_key.set(e->key.c_str(), e->key.size());
            auto ret = root_.access().lookup(art_key);
            Element* ret_element = (Element*) ret.first;

            if (ret_element == nullptr) {
                root_.access().insert(art_key, (TID) e, nullptr, txn);
            } else {
                // update
                ret_element->val = e->val;
                txn.set_version_unlock(ret_element->vers, item);
            }
        }
    }
    void unlock(TransItem& item) override {
        printf("unlock\n");
        item_val r;
        if (item.has_write()) {
            r = item.template write_value<item_val>();
        } else {
            r = item.template read_value<item_val>();
        }
        Element* e = (Element*) r.first;
        if (e != 0) {
            e->vers.cp_unlock(item);
        }
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
    TOpaqueWrapped<ART_OLC::Tree> root_;
};
