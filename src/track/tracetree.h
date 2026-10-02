/*
    SPDX-FileCopyrightText: 2014-2026 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef TRACETREE_H
#define TRACETREE_H

/**
 * @file tracetree.h
 * @brief Efficiently combine and store the data of multiple Traces.
 */

#include <tsl/robin_map.h>

#include "trace.h"

/**
 * Top-down tree of backtrace instruction pointers.
 *
 * This is supposed to be a memory efficient storage of all instruction pointers
 * ever encountered in any backtrace.
 */
class TraceTree
{
public:
    /**
     * Index the data in @p trace and return the index of the last instruction
     * pointer.
     *
     * Unknown instruction pointers will be handled by the @p callback
     */
    template <typename Fun>
    uint32_t index(const Trace& trace, Fun callback)
    {
        auto it = m_knownTraces.find(trace.hash());
        if (it != m_knownTraces.end()) {
            // fast path: use interned index
            return it.value();
        }

        // slow fallback: first bubble up and handle parent stacks by hashing the tails
        std::array<Trace::hash_t, Trace::MAX_SIZE> hashes;
        hashes[0] = trace.hash();
        uint32_t parentIndex = 0;
        int skip = 1;
        for (; skip < trace.size(); ++skip) {
            auto tailHash = trace.tailHash(skip);
            auto tailIt = m_knownTraces.find(tailHash);
            if (tailIt != m_knownTraces.end()) {
                // tail is known from this position
                parentIndex = tailIt.value();
                break;
            }
            hashes[skip] = tailHash;
        }

        if (skip == trace.size()) {
            parentIndex = 0;
        }
        --skip;

        // now output the tree in top-down order
        for (; skip >= 0; --skip) {
            auto index = m_index++;
            m_knownTraces.insert({hashes[skip], index});
            if (!callback(reinterpret_cast<uintptr_t>(trace[skip]), parentIndex)) {
                return 0;
            }
            parentIndex = index;
        }

        return parentIndex;
    }

private:
    tsl::robin_map<Trace::hash_t, uint32_t> m_knownTraces;
    uint32_t m_index = 1;
};

#endif // TRACETREE_H
