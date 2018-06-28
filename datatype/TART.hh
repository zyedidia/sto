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
    typedef std::pair<TID, ART_OLC::N*> item_key;

    struct Element {
        TKey key;
        TVal val;
        TVersion vers;
        Element* next;
    };

    static void loadKey(TID tid, Key &key) {
        Element* e = (Element*) tid;
        key.set(e->key.c_str(), e->key.size());
    }

    static TVal loadValue(TID tid) {
        Element* e = (Element*) tid;
        return e->val;
    }

    static TVersion loadVers(TID tid) {
        Element* e = (Element*) tid;
        return e->vers;
    }

    typedef typename std::conditional<true, TVersion, TNonopaqueVersion>::type Version_type;
    typedef typename std::conditional<true, TWrapped<TVal>, TNonopaqueWrapped<TVal>>::type wrapped_type;

    static constexpr TransItem::flags_type absent_bit = TransItem::user0_bit;
    static constexpr TransItem::flags_type newleaf_bit = TransItem::user0_bit<<1;
    static constexpr TransItem::flags_type head_bit = TransItem::user0_bit<<2;
    // static constexpr TransItem::flags_type deleted_bit = TransItem::user0_bit<<2;

    TART() {
        root_.access().setLoadKey(TART::loadKey);
    }

    TVal transGet(TKey k) {
        auto headItem = Sto::item(this, -1);
        headItem.add_flags(head_bit);
        Element* next = nullptr;
        if (headItem.has_write()) {
            next = headItem.template write_value<Element*>();
        }
        bool found;
        while (next) {
            if (next->key.compare(k) == 0) {
                return next->val;
            }
            next = next->next;
        }
        Key key;
        key.set(k.c_str(), k.size());
        auto r = root_.access().lookup(key);
        auto item = Sto::item(this, r);
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
        auto headItem = Sto::item(this, -1);
        headItem.add_flags(head_bit);
        Element* next = nullptr;
        if (headItem.has_write()) {
            next = headItem.template write_value<Element*>();
        }
        bool found;
        while (next) {
            if (next->key.compare(k) == 0) {
                auto item = Sto::item(this, next);
                item.add_write(v);
                // item.clear_flags(deleted_bit);
                next->val = v;
                return;
            }
            next = next->next;
        }
        Element* e = new Element();
        e->key = k;
        e->val = v;
        item_key r = {(TID) e, nullptr};
        auto item = Sto::item(this, r);

        if (headItem.has_write()) {
            e->next = headItem.template write_value<Element*>();
        } else {
            e->next = nullptr;
        }
        headItem.add_write(e);
        item.add_write(v);
        // item.clear_flags(deleted_bit);
    }

    void insert(TKey k, TVal v) {
        transPut(k, v);
    }

    void erase(TKey k) {
        auto headItem = Sto::item(this, -1);
        headItem.add_flags(head_bit);
        Element* next = nullptr;
        if (headItem.has_write()) {
            next = headItem.template write_value<Element*>();
        }
        bool found;
        while (next) {
            if (next->key.compare(k) == 0) {
                auto item = Sto::item(this, next);
                item.add_write(0);
                // item.add_flags(deleted_bit);
                next->val = 0;
                return;
            }
            next = next->next;
        }
        Element* e = new Element();
        e->key = k;
        e->val = 0;
        item_key r = {(TID) e, nullptr};
        auto item = Sto::item(this, r);

        if (headItem.has_write()) {
            e->next = headItem.template write_value<Element*>();
        } else {
            e->next = nullptr;
        }
        headItem.add_write(e);
        item.add_write(0);
        // item.add_flags(deleted_bit);
    }

    bool lock(TransItem& item, Transaction& txn) override {
        printf("lock\n");
        item_key r = item.template key<item_key>();
        if (item.has_flag(head_bit)) { return true; }
        Element* e = (Element*) r.first;
        if (e == nullptr) {
            return txn.try_lock(item, r.second->vers);
        } else {
            return txn.try_lock(item, e->vers);
        }
    }
    bool check(TransItem& item, Transaction& txn) override {
        printf("check\n");
        item_key r = item.template key<item_key>();
        if (item.has_flag(head_bit)) { return true; }
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
        item_key r = item.template key<item_key>();

        // if (item.has_flag(deleted_bit)) {
        //     art_delete(&root_.access(), c_str(key), key.length());
        //     txn.set_version(vers_);
        // } else {
        if (item.has_flag(head_bit)) { return; }
        if (r.first) {
            Element* e = (Element*) r.first;
            Key art_key;
            art_key.set(e->key.c_str(), e->key.size());
            auto ret = root_.access().lookup(art_key);
            Element* ret_element = (Element*) ret.first;

            if (ret_element == nullptr) {
                bool new_insert = false;
                if (!Sto::item(this, -1).has_flag(newleaf_bit)) {
                    new_insert = true;
                    Sto::item(this, -1).clear_flags(newleaf_bit);
                }
                root_.access().insert(art_key, (TID) e, &new_insert, txn);
            } else {
                // update
                ret_element->val = e->val;
                txn.set_version_unlock(ret_element->vers, item);
            }
        }
    }
    void unlock(TransItem& item) override {
        Element* e = item.template key<Element*>();
        if (item.has_flag(head_bit)) { return; }
        if (e != 0) {
            e->vers.cp_unlock(item);
        }
        // Sto::item(this, -1).clear_flags(deleted_bit);
        Sto::item(this, -1).clear_flags(newleaf_bit);
        Sto::item(this, -1).clear_flags(absent_bit);
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
