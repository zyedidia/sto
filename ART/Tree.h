#pragma once
//
// Created by florian on 18.11.15.
//

#ifndef ART_OPTIMISTICLOCK_COUPLING_N_H
#define ART_OPTIMISTICLOCK_COUPLING_N_H
#include "N.h"
#include "Sto.hh"

using namespace ART;

namespace ART_OLC {

    class Tree {
    public:
        using LoadKeyFunction = void (*)(TID tid, Key &key);
        TVersion absent_tvers_;

    private:
        N *const root;

        TID checkKey(const TID tid, const Key &k) const;

        LoadKeyFunction loadKey;

        // Epoche epoche{256};

    public:
        enum class CheckPrefixResult : uint8_t {
            Match,
            NoMatch,
            OptimisticMatch
        };

        enum class CheckPrefixPessimisticResult : uint8_t {
            Match,
            NoMatch,
        };

        enum class PCCompareResults : uint8_t {
            Smaller,
            Equal,
            Bigger,
        };
        enum class PCEqualsResults : uint8_t {
            BothMatch,
            Contained,
            NoMatch
        };
        static CheckPrefixResult checkPrefix(N* n, const Key &k, uint32_t &level);

        static CheckPrefixPessimisticResult checkPrefixPessimistic(N *n, const Key &k, uint32_t &level,
                                                                   uint8_t &nonMatchingKey,
                                                                   Prefix &nonMatchingPrefix,
                                                                   LoadKeyFunction loadKey, bool &needRestart);

        static PCCompareResults checkPrefixCompare(const N* n, const Key &k, uint8_t fillKey, uint32_t &level, LoadKeyFunction loadKey, bool &needRestart);

        static PCEqualsResults checkPrefixEquals(const N* n, uint32_t &level, const Key &start, const Key &end, LoadKeyFunction loadKey, bool &needRestart);

    public:

        Tree();
        Tree(LoadKeyFunction loadKey);

        void setLoadKey(LoadKeyFunction f);

        Tree(const Tree &) = delete;

        Tree(Tree &&t) : root(t.root), loadKey(t.loadKey) { }

        ~Tree();

        TID lookup(const Key &k) const;

        bool lookupRange(const Key &start, const Key &end, Key &continueKey, TID result[], std::size_t resultLen,
                         std::size_t &resultCount) const;

        void insert(const Key &k, TID tid, bool* new_insert, Transaction& txn);

        void remove(const Key &k, TID tid);
    };
}
#endif //ART_OPTIMISTICLOCK_COUPLING_N_H
