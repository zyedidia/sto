#undef NDEBUG
#include <fstream>
#include <string>
#include <thread>
#include <iostream>
#include <cassert>
#include <vector>
#include "Sto.hh"
#include "Transaction.hh"
#include <unistd.h>
#include <random>
#include <time.h>
#include "DB_index.hh"
#include "DB_params.hh"

const char* absentkey1 = "hello";
const char* absentkey2 = "1234";
const char* absentkey2_1 = "1245";
const char* absentkey2_2 = "1256";
const char* absentkey2_3 = "1267";
const char* absentkey2_4 = "1278";
const char* absentkey2_5 = "1289";

const char* checkkey = "check1";
const char* checkkey2 = "check2";
const char* checkkey3 = "check3";

class db_test_params : public db_params::db_default_params {
public:
    static constexpr db_params::db_params_id Id = db_params::db_params_id::Custom;
    static constexpr bool RdMyWr = true;
    static constexpr bool Opaque = false;
};

class keyval_db {
public:
    virtual void insert(lcdf::Str int_key, uintptr_t val) = 0;
    virtual void update(lcdf::Str int_key, uintptr_t val) = 0;
    virtual uintptr_t lookup(lcdf::Str int_key) = 0;
    virtual void erase(lcdf::Str int_key) = 0;
};

class masstree_wrapper : public keyval_db {
public:
    struct oi_value {
        enum class NamedColumn : int { val = 0 };
        uintptr_t val;
    };

    typedef bench::ordered_index<lcdf::Str, oi_value, db_test_params> index_type;
    index_type oi;

    masstree_wrapper() {
    }

    void insert(lcdf::Str key, uintptr_t val) override {
        bool success;
        std::tie(success, std::ignore) = oi.insert_row(key, new oi_value{val});
        if (!success) throw Transaction::Abort();
    }

    uintptr_t lookup(lcdf::Str key) override {
        uintptr_t ret;
        bool success;
        const oi_value* val;
        std::tie(success, std::ignore, std::ignore, val) = oi.select_row(key, bench::RowAccess::ObserveValue);
        if (!success) throw Transaction::Abort();
        if (!val) {
            ret = 0;
        } else {
            ret = val->val;
        }
        return ret;
    }

    void update(lcdf::Str key, uintptr_t val) override {
        bool success;
        uintptr_t row;
        const oi_value* value;
        std::tie(success, std::ignore, row, value) = oi.select_row(key, bench::RowAccess::UpdateValue);
        if (!success) throw Transaction::Abort();
        auto new_oiv = Sto::tx_alloc<oi_value>(value);
        new_oiv->val = val;
        oi.update_row(row, new_oiv);
    }

    void erase(lcdf::Str key) override {
        oi.delete_row(key);
    }
};

class tart_wrapper : public keyval_db {
public:
    struct oi_value {
        enum class NamedColumn : int { val = 0 };
        uintptr_t val;
    };

    typedef bench::art_index<lcdf::Str, oi_value, db_test_params> index_type;
    index_type oi;

    tart_wrapper() {
    }

    void insert(const char* key, uintptr_t val) {
        insert({key, sizeof(key)}, val);
    }

    void insert(lcdf::Str key, uintptr_t val) override {
        bool success;
        std::tie(success, std::ignore) = oi.insert_row(key, new oi_value{val});
        if (!success) throw Transaction::Abort();
    }

    uintptr_t lookup(const char* key) {
        return lookup({key, sizeof(key)});
    }

    uintptr_t lookup(lcdf::Str key) override {
        uintptr_t ret;
        bool success;
        bool found;
        const oi_value* val;
        std::tie(success, std::ignore, std::ignore, val) = oi.select_row(key, bench::RowAccess::ObserveValue);
        if (!success) throw Transaction::Abort();
        if (!val) {
            ret = 0;
        } else {
            ret = val->val;
        }
        return ret;
    }

    void update(const char* key, uintptr_t val) {
        update({key, sizeof(key)}, val);
    }

    void update(lcdf::Str key, uintptr_t val) override {
        bool success;
        uintptr_t row;
        const oi_value* value;
        std::tie(success, std::ignore, row, value) = oi.select_row(key, bench::RowAccess::UpdateValue);
        if (!success) throw Transaction::Abort();
        auto new_oiv = Sto::tx_alloc<oi_value>(value);
        new_oiv->val = val;
        oi.update_row(row, new_oiv);
    }

    void erase(const char* key) {
        erase({key, sizeof(key)});
    }

    void erase(lcdf::Str key) override {
        oi.delete_row(key);
    }
};

typedef tart_wrapper wrapper_type;

void testSimple() {
    wrapper_type a;

    const char* key1 = "hello world";
    const char* key2 = "1234";
    {
        TransactionGuard t;
        a.insert(key1, 123);
        a.insert(key2, 321);
    }

    {
        TransactionGuard t;
        volatile auto x = a.lookup(key1);
        volatile auto y = a.lookup(key2);
        assert(x == 123);
        assert(y == 321);
    }

    // {
    //     TransactionGuard t;
    //     a.insert("foo", 1);
    //     a.insert("foobar", 2);
    // }
    //
    // {
    //     TransactionGuard t;
    //     assert(a.lookup("foobar") == 2);
    // }


    printf("PASS: %s\n", __FUNCTION__);
}

void testSimpleErase() {
    wrapper_type a;

    const char* key1 = "hello world";
    const char* key2 = "1234";
    {
        TransactionGuard t;
        a.insert(key1, 123);
        a.insert(key2, 321);
    }

    {
        TransactionGuard t;
        volatile auto x = a.lookup(key1);
        volatile auto y = a.lookup(key2);
        a.insert(checkkey, 100);
        assert(x == 123);
        assert(y == 321);
    }

    {
        TransactionGuard t;
        a.erase(key1);
        volatile auto x = a.lookup(key1);
        a.insert(checkkey, 100);
        assert(x == 0);
    }

    {
        TransactionGuard t;
        volatile auto x = a.lookup(key1);
        assert(x == 0);
        a.insert(key1, 567);
    }

    {
        TransactionGuard t;
        volatile auto x = a.lookup(key1);
        a.insert(checkkey, 100);
        assert(x == 567);
    }

    printf("PASS: %s\n", __FUNCTION__);
}

void testAbsentErase() {
    wrapper_type a;

    TestTransaction t1(0);
    a.erase("foo");
    a.insert("bar", 1);

    TestTransaction t2(1);
    a.insert("foo", 123);
    assert(t2.try_commit());

    assert(!t1.try_commit());
    printf("PASS: %s\n", __FUNCTION__);
}

void multiWrite() {
    wrapper_type aTART;
    {
        TransactionGuard t;
        aTART.insert(absentkey2, 456);
    }

    {
        TransactionGuard t;
        aTART.update(absentkey2, 123);
    }
    {
        TransactionGuard t;
        volatile auto x = aTART.lookup(absentkey2);
        assert(x == 123);    
    }
    printf("PASS: %s\n", __FUNCTION__);
}

void testEmptyErase() {
    wrapper_type a;

    const char* key1 = "hello world";

    // deleting non-existent node
    {
        TransactionGuard t;
        a.erase(key1);
        volatile auto x = a.lookup(key1);
        a.insert(checkkey, 100);
        assert(x == 0);
    }

    {
        TransactionGuard t;
        a.erase(key1);
        volatile auto x = a.lookup(key1);
        assert(x == 0);
        a.insert(key1, 123);
        a.erase(key1);
        x = a.lookup(key1);
        assert(x == 0);    
    }

    printf("PASS: %s\n", __FUNCTION__);
}

void testUpgradeNode() {
    wrapper_type a;
    {
        TransactionGuard t;
        a.insert("1", 1);
        a.insert("10", 1);
        a.insert("11", 1);
        a.insert("12", 1);
        a.insert("15", 1);
    }

    TestTransaction t0(0);
    a.lookup("13");
    a.insert("14", 1);

    TestTransaction t1(1);
    a.insert("13", 1);

    assert(t1.try_commit());
    assert(!t0.try_commit());

    printf("PASS: %s\n", __FUNCTION__); 
}

void testUpgradeNode2() {
    wrapper_type a;
    {
        TransactionGuard t;
        a.insert("1", 1);
        a.insert("10", 1);
        // a.insert("11", 1);
    }

    TestTransaction t0(0);
    a.lookup("13");
    a.insert("14", 1);
    a.insert("15",1);
    a.insert("16", 1);
    assert(t0.try_commit());

    TestTransaction t1(1);
    a.lookup("13");
    a.insert("14", 1);
    a.insert("15",1);
    a.insert("16", 1);

    assert(t1.try_commit());
    printf("PASS: %s\n", __FUNCTION__); 
}


void testUpgradeNode3() {
    wrapper_type a;
    {
        TransactionGuard t;
        a.insert("10", 1);
        a.insert("11", 1);
    }

    TestTransaction t0(0);
    a.lookup("13");

    TestTransaction t1(1);
    a.insert("13", 1);

    t0.use();
    a.insert("14", 1);

    assert(t1.try_commit());
    assert(!t0.try_commit());

    printf("PASS: %s\n", __FUNCTION__); 
}

void testUpgradeNode4() {
    wrapper_type a;
    {
        TransactionGuard t;
        a.insert("10", 1);
        a.insert("11", 1);
        a.insert("12", 1);
    }

    TestTransaction t0(0);
    a.lookup("14");

    TestTransaction t1(1);
    a.insert("13", 1);
    a.erase("10");
    a.erase("11");
    a.erase("11");
    a.erase("13");
    a.insert("14", 1);
    a.insert("15", 1);
    a.insert("16", 1);
    a.insert("17", 1);

    assert(t1.try_commit());

    t0.use();
    a.erase("14");
    a.erase("15");
    a.erase("16");
    a.erase("17");

    assert(!t0.try_commit());

    printf("PASS: %s\n", __FUNCTION__); 
}

void testDowngradeNode() {
    wrapper_type a;

    {
        TransactionGuard t;
        a.insert("1", 1);
        a.insert("10", 1);
        a.insert("11", 1);
        a.insert("12", 1);
        a.insert("13", 1);
        a.insert("14", 1);
    }

    TestTransaction t0(0);
    a.lookup("15");
    a.insert("random", 1);

    TestTransaction t1(1);
    a.lookup("10");
    a.insert("hummus", 1);

    TestTransaction t2(2);
    a.erase("15");
    a.insert("linux", 1);

    TestTransaction t3(1);
    a.erase("14");
    a.erase("13");
    a.erase("12");
    a.erase("11");
    assert(t3.try_commit());

    assert(!t0.try_commit());
    assert(t1.try_commit());
    assert(!t2.try_commit());
    printf("PASS: %s\n", __FUNCTION__);
}

void testSplitNode() {
    wrapper_type a;
    {
        TransactionGuard t;
        a.insert("ab", 0);
        a.insert("1", 0);
    }

    TestTransaction t0(0);
    a.lookup("ad");
    a.insert("12", 1);

    TestTransaction t1(1);
    a.lookup("abc");
    a.insert("13", 1);

    TestTransaction t2(2);
    a.insert("ad", 1);
    a.insert("abc", 1);

    assert(t2.try_commit());
    assert(!t0.try_commit());
    assert(!t1.try_commit());

    printf("PASS: %s\n", __FUNCTION__); 
}

void testSplitNode2() {
    wrapper_type a;
    {
        TransactionGuard t;
        a.insert("aaa", 0);
        a.insert("aab", 0);
    }

    TestTransaction t0(0);
    a.lookup("ab");
    a.insert("1", 0);

    TestTransaction t1(1);
    a.insert("ab", 0);

    assert(t1.try_commit());
    assert(!t0.try_commit());

    printf("PASS: %s\n", __FUNCTION__); 
}

void testEmptySplit() {
    wrapper_type a;
    {
        TransactionGuard t;
        a.insert("aaa", 1);
        a.insert("aab", 1);
    }

    TestTransaction t0(0);
    a.lookup("aac");
    a.insert("1", 0);

    TestTransaction t1(1);
    a.erase("aaa");
    a.erase("aab");
    assert(t1.try_commit());

    TestTransaction t2(2);
    a.insert("aac", 0);
    assert(t2.try_commit());

    assert(!t0.try_commit());

    printf("PASS: %s\n", __FUNCTION__); 
}

void testDoubleRead() {
    wrapper_type a;

    TestTransaction t0(0);
    auto r = a.lookup("1");

    TestTransaction t1(1);
    a.insert("1", 50);
    assert(t1.try_commit());

    TestTransaction t2(2);
    a.update("1", 100);
    a.insert("2", 50);
    assert(t2.try_commit());

    t0.use();
    try {
        auto s = a.lookup("2");
        assert(!t0.try_commit());
    } catch (Transaction::Abort e) {
    }

    printf("PASS: %s\n", __FUNCTION__);
}

void testReadWrite() {
    wrapper_type art;
    {
        TransactionGuard t;
        art.insert("hello", 4);
    }

    TestTransaction t1(0);
    auto r = art.lookup("hello");
    art.insert("world", 1);

    TestTransaction t2(1);
    art.update("hello", 6);
    assert(t2.try_commit());
    t1.use();
    auto s = art.lookup("hello");
    assert(!t1.try_commit());

    printf("PASS: %s\n", __FUNCTION__);
}

int main(int argc, char *argv[]) {
    testSimple();
    testSimpleErase();
    testAbsentErase();
    testEmptyErase();
    multiWrite();
    testUpgradeNode();
    testUpgradeNode2();
    testUpgradeNode3();
    testUpgradeNode4();
    testDowngradeNode();
    testSplitNode();
    testSplitNode2();
    testEmptySplit();
    testDoubleRead();
    testReadWrite();

    printf("Tests pass\n");
}
