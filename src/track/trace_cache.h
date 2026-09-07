#ifndef TRACECACHE_H
#define TRACECACHE_H

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>

#include "trace.h"

/**
 * A small, fixed-size LRU cache mapping backtraces to TraceTree indices.
 *
 * Placed in front of TraceTree::index() to skip expensive trie walks for
 * repeated backtraces. Uses a 32-bit hash key with IP-level verification
 * (first HASH_DEPTH frames via memcmp) to prevent false positives. Eviction
 * uses a decay counter: each miss decays the LRU tail by 25%, entries drop
 * below HITS_THRESHOLD after ~4 consecutive misses and become eligible for
 * replacement.
 *
 * Cache-line layout is optimized for the hot path: m_key in CL0 is always
 * read; m_hitCount/m_index in CL1 are only touched on hit; m_ips in CL2-5
 * are only read after a key match.
 */
class alignas(64) TraceCache
{
public:
    static constexpr uint32_t HITS_THRESHOLD = 4;
    static constexpr uint32_t HITS_INITIAL = 8;
    static constexpr int HASH_DEPTH = 4;

    // Returns 0 on miss. On hit: increment hits, bubble up one position.
    [[gnu::always_inline]] uint32_t lookup(const Trace& trace)
    {
        const size_t key = trace.hashkey();
        const Trace::ip_t* const ips = trace.begin();
        const int depth = std::min(trace.size(), HASH_DEPTH);

        int match = -1;
        for (int i = 0; i < SIZE; ++i) {
            if (m_key[i] == key) {
                match = i;
                break;
            }
        }

        if (match < 0)
            return 0;

        // Compare IPs: fast path when depth == HASH_DEPTH uses memcmp
        if (__builtin_expect(depth == HASH_DEPTH, 1)) {
            if (memcmp(m_ips[match].data(), ips, HASH_DEPTH * sizeof(Trace::ip_t)) != 0)
                return 0;
        } else {
            if (memcmp(m_ips[match].data(), ips, depth * sizeof(Trace::ip_t)) != 0)
                return 0;
        }

        ++m_hitCount[match];
        const int rank = m_lruRank[match];
        if (rank > 0) {
            const int prevSlot = m_lruSlot[rank - 1];
            m_lruSlot[rank] = prevSlot;
            m_lruSlot[rank - 1] = match;
            m_lruRank[prevSlot] = rank;
            m_lruRank[match] = rank - 1;
        }

        return m_index[match];
    }

    // Decay the LRU tail's hit counter; evict only when it drops below HITS_THRESHOLD.
    [[gnu::always_inline]] void store(const Trace& trace, uint32_t index)
    {
        assert(index != 0);
        const int victim = m_lruSlot[SIZE - 1];

        if (m_index[victim] != 0) {
            m_hitCount[victim] -= m_hitCount[victim] >> 2;
            if (m_hitCount[victim] >= HITS_THRESHOLD)
                return;
        }

        m_key[victim] = trace.hashkey();
        m_hitCount[victim] = HITS_INITIAL;
        m_index[victim] = index;
        const int depth = std::min(trace.size(), HASH_DEPTH);
        m_ips[victim] = {};
        memcpy(m_ips[victim].data(), trace.begin(), depth * sizeof(Trace::ip_t));
    }

    void clear()
    {
        for (int i = 0; i < SIZE; ++i) {
            m_key[i] = 0;
            m_hitCount[i] = 0;
            m_index[i] = 0;
            m_ips[i] = {};
            m_lruSlot[i] = i;
            m_lruRank[i] = i;
        }
    }

private:
    static constexpr int SIZE = 8;

    // CL0 (hot): m_key[8] = 64 bytes, read on every lookup
    size_t m_key[SIZE] = {};
    // CL1 (warm): m_hitCount[8] + m_index[8] = 64 bytes, read/write on hit
    uint32_t m_hitCount[SIZE] = {};
    uint32_t m_index[SIZE] = {};
    // CL2-5 (cold): m_ips[8] = 256 bytes, read only after key match
    std::array<Trace::ip_t, HASH_DEPTH> m_ips[SIZE] = {};
    // CL6: m_lruSlot[8] + m_lruRank[8] = 16 bytes, read/write on hit
    //   m_lruSlot[rank] -> cache slot at LRU rank  (0 = MRU, SIZE-1 = LRU)
    //   m_lruRank[slot] -> LRU rank of cache slot
    uint8_t m_lruSlot[SIZE] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint8_t m_lruRank[SIZE] = {0, 1, 2, 3, 4, 5, 6, 7};
};

#endif // TRACECACHE_H