#undef NDEBUG
#include <string>
#include <thread>
#include <iostream>
#include <cassert>
#include <vector>
#include "Sto.hh"
#include "TART.hh"
#include "Transaction.hh"
#include <unistd.h>

struct Element {
    const char* key;
    int len;
    uintptr_t val;
    TVersion vers;
    bool poisoned;
};

#define GUARDED if (TransactionGuard tguard{})

// TODO these cause simple 2 to fail
// const char* absentkey1 = "he";
// const char* absentkey2 = "hello";

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

void process_mem_usage(double& vm_usage, double& resident_set) {
    vm_usage     = 0.0;
    resident_set = 0.0;

    // the two fields we want
    unsigned long vsize;
    long rss;
    {
        std::string ignore;
        std::ifstream ifs("/proc/self/stat", std::ios_base::in);
        ifs >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore
                >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore
                >> ignore >> ignore >> vsize >> rss;
    }

    long page_size_kb = sysconf(_SC_PAGE_SIZE) / 1024; // in case x86-64 is configured to use 2MB pages
    vm_usage = vsize / 1024.0;
    resident_set = rss * page_size_kb;
}

void NoChecks() {
    TART aTART;
    {
        TransactionGuard t;
        aTART.insert(absentkey1, 10);
    }
    {
        TransactionGuard t;
        aTART.lookup(absentkey1);
    }

    {
        TransactionGuard t;
        aTART.erase(absentkey1);
    }

    {
        TransactionGuard t;
        aTART.insert(absentkey1, 10);
        aTART.lookup(absentkey1);
        aTART.erase(absentkey1);
    }
        // Insert check print statement, no check should occur
}

void Checks() {
    TART aTART;
    {
        TransactionGuard t;
        aTART.insert(absentkey1, 10);
    }
    printf("1. ");
    {
        TransactionGuard t;
        auto x = aTART.lookup(absentkey1);
        aTART.insert(checkkey, 100);
        if(x == 0) {
            printf("wtf\n");
        }
    }
    printf("\n2.");
    {
        TransactionGuard t;
        aTART.lookup(absentkey1);
        aTART.insert(absentkey2, 12);
    }
    printf("\n3.");
    {
        TransactionGuard t;
        aTART.lookup(absentkey1);
        aTART.erase(absentkey2);
    }
    printf("\n4.");
    {
        TransactionGuard t;
        volatile auto x = aTART.lookup(absentkey1);
        aTART.insert(checkkey, 100);

        if (x == 0) {
            printf("wtf\n");
        }
    }
    printf("\n");
    printf("PASS: %s\n", __FUNCTION__);

}

void testSimple() {
    TART a;

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

    {
        TransactionGuard t;
        a.insert("foo", 1);
        a.insert("foobar", 2);
    }

    {
        TransactionGuard t;
        assert(a.lookup("foobar") == 2);
    }


    printf("PASS: %s\n", __FUNCTION__);
}

void testSimple2() {
    TART aTART;

    {
        TransactionGuard t;
        aTART.insert(absentkey1, 10);
        aTART.insert(absentkey2, 10);
    }

    TestTransaction t1(0);
    aTART.lookup(absentkey1);
    aTART.insert(absentkey2, 123);

    TestTransaction t2(0);
    aTART.insert(absentkey2, 456);

    assert(t2.try_commit());
    assert(t1.try_commit());

    {
        TransactionGuard t;
        volatile auto x = aTART.lookup(absentkey2);
        assert(x == 123);
    }
    printf("PASS: %s\n", __FUNCTION__);
}

void testSimpleErase() {
    TART a;

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

void testEmptyErase() {
    TART a;

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

void testAbsentErase() {
    TART a;

    TestTransaction t1(0);
    a.erase("foo");
    a.insert("bar", 1);

    TestTransaction t2(1);
    a.insert("foo", 123);
    assert(t2.try_commit());

    t1.use();
    assert(!t1.try_commit());
    printf("PASS: %s\n", __FUNCTION__);
}

void multiWrite() {
    TART aTART;
    {
        TransactionGuard t;
        aTART.insert(absentkey2, 456);
    }

    {
        TransactionGuard t;
        aTART.insert(absentkey2, 123);
    }
    {
        TransactionGuard t;
        volatile auto x = aTART.lookup(absentkey2);
        assert(x == 123);    
    }
    printf("PASS: %s\n", __FUNCTION__);
}

void multiThreadWrites() {
    TART aTART;
    {
        TransactionGuard t;
        aTART.insert(absentkey2, 456);
    }

    TestTransaction t1(0);
    aTART.insert(absentkey2, 123);

    TestTransaction t2(0);
    aTART.insert(absentkey2, 456);

    assert(t1.try_commit());
    assert(t2.try_commit());

    {
        TransactionGuard t;
        // printf("to lookup\n");
        volatile auto x = aTART.lookup(absentkey2);
        // printf("looked\n");
        assert(x == 456);
    }
    printf("PASS: %s\n", __FUNCTION__);

}

void testReadDelete() {
    TART aTART;
    TestTransaction t0(0);
    aTART.insert(absentkey1, 10);
    aTART.insert(absentkey2, 10);
    assert(t0.try_commit());

    TestTransaction t1(0);
    aTART.lookup(absentkey1);
    aTART.insert(absentkey2, 10);

    TestTransaction t2(0);
    aTART.erase(absentkey1);

    assert(t2.try_commit());
    assert(!t1.try_commit());

    {
        TransactionGuard t;
        volatile auto x = aTART.lookup(absentkey1);
        volatile auto y = aTART.lookup(absentkey2);
        assert(x == 0);
        assert(y == 10);
    }

    printf("PASS: %s\n", __FUNCTION__);
}

void testReadWriteDelete() {
    TART aTART;
    TestTransaction t0(0);
    aTART.insert(absentkey1, 10);
    aTART.insert(absentkey2, 10);
    assert(t0.try_commit());

    TestTransaction t1(0);
    aTART.lookup(absentkey1);
    aTART.insert(absentkey2, 123);

    TestTransaction t2(0);
    aTART.erase(absentkey1);

    assert(t2.try_commit());
    assert(!t1.try_commit());

    {
        TransactionGuard t;
        volatile auto x = aTART.lookup(absentkey1);
        volatile auto y = aTART.lookup(absentkey2);
        assert(x == 0);
        assert(y == 10);
    }

    printf("PASS: %s\n", __FUNCTION__);
}

void testReadDeleteInsert() {
    TART aTART;
    TestTransaction t0(0);
    aTART.insert(absentkey1, 10);
    aTART.insert(absentkey2, 10);
    assert(t0.try_commit());

    TestTransaction t1(0);
    aTART.lookup(absentkey1);
    aTART.insert(absentkey2, 123);

    TestTransaction t2(0);
    aTART.erase(absentkey1);
    assert(t2.try_commit());

    TestTransaction t3(0);
    aTART.insert(absentkey1, 10);
    assert(t3.try_commit());
    assert(!t1.try_commit());

    {
        TransactionGuard t;
        volatile auto x = aTART.lookup(absentkey1);
        volatile auto y = aTART.lookup(absentkey2);
        assert(x == 10);
        assert(y == 10);
    }

    printf("PASS: %s\n", __FUNCTION__);
}


void testInsertDelete() {
    TART aTART;
    TestTransaction t0(0);
    aTART.insert(absentkey1, 10);
    aTART.insert(absentkey2, 10);
    assert(t0.try_commit());

    TestTransaction t1(0);
    aTART.insert(absentkey1, 123);
    aTART.insert(absentkey2, 456);

    TestTransaction t2(0);
    aTART.erase(absentkey1);
    assert(t2.try_commit());

    assert(!t1.try_commit());

    {
        TransactionGuard t;
        volatile auto x = aTART.lookup(absentkey1);
        volatile auto y = aTART.lookup(absentkey2);
        assert(x == 0);
        assert(y == 10);
    }

    printf("PASS: %s\n", __FUNCTION__);
}

// test that reading poisoned val aborts
void testAbsent1_1() {
    TART aTART;

    TestTransaction t1(0);
    aTART.lookup(absentkey1);
    // a new insert
    TestTransaction t2(0);
    aTART.insert(absentkey1, 456);
    aTART.insert(absentkey2, 123);

    t1.use();
    try {
        aTART.lookup(absentkey2);
    } catch (Transaction::Abort e) { 
        assert(t2.try_commit());
        {
            TransactionGuard t;
            volatile auto x = aTART.lookup(absentkey1);
            assert(x == 456);
        }
        printf("PASS: %s\n", __FUNCTION__);
        return;
    }
    assert(false);


}

// test you can write to a key after absent reading it
void testAbsent1_2() {
    TART aTART;

    TestTransaction t1(0);
    aTART.lookup(absentkey1);
    aTART.insert(absentkey1, 123);

    // a new insert
    TestTransaction t2(0);
    try {
        aTART.insert(absentkey1, 456);
    } catch (Transaction::Abort e) {}

    assert(t1.try_commit());

    {
        TransactionGuard t;
        volatile auto x = aTART.lookup(absentkey1);
        assert(x == 123);
    }

    printf("PASS: %s\n", __FUNCTION__);
}

// test that absent read detects changes made by other threads
void testAbsent1_3() {
    TART aTART;

    TestTransaction t1(0);
    aTART.lookup(absentkey1);
    aTART.insert(absentkey2, 123);

    // a new insert
    TestTransaction t2(0);
    aTART.insert(absentkey1, 456);

    assert(t2.try_commit());
    
    TestTransaction t3(0);
    aTART.erase(absentkey1);

    assert(t3.try_commit());
    assert(!t1.try_commit());

    {
        TransactionGuard t;
        volatile auto x = aTART.lookup(absentkey1);
        assert(x == 0);
    }

    printf("PASS: %s\n", __FUNCTION__);
}

// 
void testAbsent2_2() {
    TART aTART;

    TestTransaction t1(0);
    aTART.lookup(absentkey1);
    aTART.insert(absentkey1, 123);
    aTART.lookup(absentkey2);
    aTART.insert(absentkey2, 123);

    // a new insert
    TestTransaction t2(0);
    try {
        aTART.insert(absentkey1, 456);
    } catch (Transaction::Abort e) {}

    assert(t1.try_commit());

    {
        TransactionGuard t;
        volatile auto x = aTART.lookup(absentkey1);
        assert(x == 123);
    }

    printf("PASS: %s\n", __FUNCTION__);

}

void testAbsent3() {
    TART aTART;

    TestTransaction t0(0);
    aTART.insert(absentkey2, 123);
    aTART.insert(absentkey2_1, 456);

    assert(t0.try_commit());

    TestTransaction t1(0);
    aTART.lookup(absentkey1);
    aTART.insert(absentkey2, 123);

    // an update
    TestTransaction t2(0);
    aTART.insert(absentkey2_2, 456);

    assert(t2.try_commit());
    assert(t1.try_commit());

    {
        TransactionGuard t;
        volatile auto x = aTART.lookup(absentkey1);
        volatile auto y = aTART.lookup(absentkey2);
        assert(y == 123);
        assert(x == 0);
    }

    printf("PASS: %s\n", __FUNCTION__);
}

void testAbsent3_2() {
    TART aTART;

    TestTransaction t0(0);
    aTART.insert(absentkey2, 123);
    aTART.insert(absentkey2_1, 123);
    aTART.insert(absentkey2_2, 123);
    aTART.insert(absentkey2_3, 123);
    assert(t0.try_commit());

    TestTransaction t1(0);
    aTART.lookup(absentkey2_4);
    aTART.insert(absentkey2, 123);

    // an update
    TestTransaction t2(0);
    aTART.insert(absentkey2_5, 456);

    assert(t2.try_commit());
    assert(!t1.try_commit());

    {
        TransactionGuard t;
        volatile auto x = aTART.lookup(absentkey1);
        volatile auto y = aTART.lookup(absentkey2);
        assert(y == 123);
        assert(x == 0);
    }

    printf("PASS: %s\n", __FUNCTION__);
}

void testABA1() {
    TART aTART;
    {
        TransactionGuard t;
        aTART.insert(absentkey2, 456);
    }

    TestTransaction t1(0);
    aTART.lookup(absentkey2);
    aTART.insert(absentkey1, 123);

    TestTransaction t2(0);
    aTART.erase(absentkey2);
    assert(t2.try_commit());

    TestTransaction t3(0);
    aTART.insert(absentkey2, 456);

    assert(t3.try_commit());
    assert(!t1.try_commit());


    {
        TransactionGuard t;
        volatile auto x = aTART.lookup(absentkey1);
        volatile auto y = aTART.lookup(absentkey2);
        assert(y == 456);
        assert(x == 0);
    }

    printf("PASS: %s\n", __FUNCTION__);

    // ABA1 should fail due to key2 value changing
}

void testMultiRead() {
    TART art;
    {
        TransactionGuard t;
        art.insert("hello", 4);
    }

    TestTransaction t1(0);
    volatile auto x = art.lookup("hello");

    TestTransaction t2(1);
    volatile auto y = art.lookup("hello");
    assert(t2.try_commit());

    t1.use();
    assert(t1.try_commit());
    assert(x == y);
    assert(x == 4);
    printf("PASS: %s\n", __FUNCTION__);
}

void testReadWrite() {
    TART art;
    {
        TransactionGuard t;
        art.insert("hello", 4);
    }

    TestTransaction t1(0);
    art.lookup("hello");
    art.insert("world", 1);

    TestTransaction t2(1);
    art.insert("hello", 6);
    assert(t2.try_commit());
    assert(!t1.try_commit());

    printf("PASS: %s\n", __FUNCTION__);
}

void testPerNodeV() {
    TART art;
    {
        TransactionGuard t;
        art.insert("x", 1);
        art.insert("y", 2);
        art.insert("z", 3);
    }

    {
        TransactionGuard t;
        volatile auto x = art.lookup("x");
        volatile auto y = art.lookup("y");
        volatile auto z = art.lookup("z");
        assert(x == 1);
        assert(y == 2);
        assert(z == 3);
    }

    TestTransaction t1(0);
    art.lookup("x");
    art.insert("z", 13);

    TestTransaction t2(1);
    art.insert("y", 12);

    assert(t2.try_commit());
    assert(t1.try_commit());

    {
        TransactionGuard t;
        volatile auto x = art.lookup("x");
        volatile auto y = art.lookup("y");
        volatile auto z = art.lookup("z");
        assert(x == 1);
        assert(y == 12);
        assert(z == 13);
    }
    printf("PASS: %s\n", __FUNCTION__);
}

int main() {
    pthread_t advancer;
    pthread_create(&advancer, NULL, Transaction::epoch_advancer, NULL);
    pthread_detach(advancer); 

    TART* a = new TART();

    // uintptr_t* result = new uintptr_t[10];
    // size_t resultsFound;
    // {
    //     TransactionGuard t;
    //     a->insert("romane", 1);
    //     a->insert("romanus", 2);
    //     a->insert("romulus", 3);
    //     a->insert("rubens", 4);
    //     a->insert("ruber", 5);
    //     a->insert("rubicon", 6);
    //     a->insert("rubicundus", 7);
    //
    //     a->erase("romanus");
    //
    //     bool success = a->lookupRange({"romane", 7}, {"ruber", 6}, {"", 0}, result, 10, resultsFound);
    //     printf("success: %d\n", success);
    //     for (int i = 0; i < resultsFound; i++) {
    //         printf("%d: %d\n", resultsFound, result[i]);
    //     }
    // }

    // a->print();

    testSimple();
    testSimple2();
    testSimpleErase();
    testEmptyErase();
    testAbsentErase();
    multiWrite();
    multiThreadWrites();
    testReadDelete(); // problem w/ lacking implementation of erase
    testReadWriteDelete();
    testReadDeleteInsert();
    testAbsent1_1();
    testInsertDelete();
    testAbsent1_2();
    testAbsent1_3(); // ABA read insert delete detection no longer exists
    testAbsent2_2();
    testAbsent3();
    testAbsent3_2();
    testABA1(); // ABA doesn't work
    testMultiRead();
    testReadWrite();
    testPerNodeV();
    testReadWrite();

    double vm_usage; double resident_set;
    process_mem_usage(vm_usage, resident_set);
    printf("Before insert\n");
    printf("RSS: %f, VM: %f\n", resident_set, vm_usage);

    for (int i = 0; i < 1000000; i++) {
        TRANSACTION_E {
            a->insert(i, i);
        } RETRY_E(true);
    }

    process_mem_usage(vm_usage, resident_set);
    printf("After insert\n");
    printf("RSS: %f, VM: %f\n", resident_set, vm_usage);

    for (int i = 0; i < 1000000; i++) {
        TRANSACTION_E {
            a->erase(i);
        } RETRY_E(true);
    }

    process_mem_usage(vm_usage, resident_set);
    printf("After erase\n");
    printf("RSS: %f, VM: %f\n", resident_set, vm_usage);

    for (int i = 0; i < 1000000; i++) {
        TRANSACTION_E {
            a->insert(i, i);
        } RETRY_E(true);
    }

    process_mem_usage(vm_usage, resident_set);
    printf("After re-insert\n");
    printf("RSS: %f, VM: %f\n", resident_set, vm_usage);

    printf("TART tests pass\n");

    return 0;
}
