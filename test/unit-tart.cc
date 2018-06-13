#undef NDEBUG
#include <string>
#include <iostream>
#include <cassert>
#include <vector>
#include "Sto.hh"
#include "TART.hh"
#include "Transaction.hh"

#define GUARDED if (TransactionGuard tguard{})

void testSimple() {
    TART a;

    uint8_t key1[4] = {0, 0, 0, 0};
    uint8_t key2[4] = {0, 0, 0, 1};
    {
        TransactionGuard t;
        a.transPut(key1, 123);
        a.transPut(key2, 321);
    }

    {
        TransactionGuard t2;
        auto x = a.transGet(key1);
        auto y = a.transGet(key2);
        assert(x.second == 123);
        assert(y.second == 321);
    }

    printf("PASS: %s\n", __FUNCTION__);
}

int main() {
    testSimple();
    return 0;
}
