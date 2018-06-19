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

#define NTHREAD 100
#define NVALS 1000

TART art;
std::string keys[NVALS];
int vals[NVALS];

std::string rand_string() {
    auto randchar = []() -> char
    {
        const char charset[] = "abcdefghijklmnopqrstuvwxyz";
        const size_t max_index = (sizeof(charset) - 1);
        return charset[ rand() % max_index ];
    };
    std::string str(10, 0);
    std::generate_n(str.begin(), 10, randchar);
    return str;
}

int rand_int() {
    return std::rand();
}

void doBench() {
    TRANSACTION_E {
        for (int i = 0; i < 10; i++) {
            auto keyI = rand_int() % NVALS;
            auto valI = rand_int() % NVALS;
            auto eraseI = rand_int() % NVALS;
            art.insert(keys[keyI], vals[valI]);
            assert(art.lookup(keys[keyI]) == vals[valI]);
            art.erase(keys[eraseI]);
        }
    } RETRY_E(true);
}

int main() {
    srand(time(NULL));
    art = TART();

    for (int i = 0; i < NVALS; i++) {
        keys[i] = rand_string();
        vals[i] = rand_int();
        printf("%d: key: %s, val: %d\n", i, keys[i].c_str(), vals[i]);
    }

    std::thread threads[NTHREAD];

    for (int i = 0; i < NTHREAD; i++) {
        threads[i] = std::thread(doBench);
    }

    for (int i = 0; i < NTHREAD; i++) {
        threads[i].join();
    }
}
