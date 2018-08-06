#pragma once
//
// Created by florian on 18.11.15.
//

#ifndef ART_OPTIMISTICLOCK_COUPLING_N_H
#define ART_OPTIMISTICLOCK_COUPLING_N_H
#include "N.h"

using namespace ART;

namespace ART_OLC {
    class Tree {
    public:
        using LoadKeyFunction = void (*)(TID tid, Key &key);
        typedef std::tuple<TID, N*, N*> ins_return_type; // (success, found)

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

        std::pair<TID, N*> lookup(const Key &k) const;

        bool lookupRange(const Key &start, const Key &end, std::function<void(N*)> observe_node, std::function<bool(const Key &k, TID t)> observe_value) const;

        ins_return_type insert(const Key &k, std::function<TID()> make_tid);

        N* remove(const Key &k, TID tid);
        void print() const;
    };
}
#endif //ART_OPTIMISTICLOCK_COUPLING_N_H
