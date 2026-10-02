/*
    SPDX-FileCopyrightText: 2014-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef TRACE_H
#define TRACE_H

#include <cassert>
#include <cstdint>

#include <boost/functional/hash.hpp>
#include <xxhash.h>

/**
 * @brief Backtrace interface.
 */
struct Trace
{
    using ip_t = void*;
    struct hash_t
    {
        XXH128_hash_t value = {};

        bool operator==(const hash_t& rhs) const
        {
            return value.low64 == rhs.value.low64 && value.high64 == rhs.value.high64;
        }

        bool operator!=(const hash_t& rhs) const
        {
            return !operator==(rhs);
        }
    };

    enum : int
    {
        MAX_SIZE = 64
    };

    const ip_t* begin() const
    {
        return m_data + m_skip;
    }

    const ip_t* end() const
    {
        return begin() + m_size;
    }

    ip_t operator[](int i) const
    {
        return m_data[m_skip + i];
    }

    int size() const
    {
        return m_size;
    }

    /// a hash that represents the computed backtrace
    hash_t hash() const
    {
        return m_hash;
    }

    bool fill(int skip)
    {
        int size = unwind(m_data);
        // filter bogus frames at the end, which sometimes get returned by tracer backend
        // cf.: https://bugs.kde.org/show_bug.cgi?id=379082
        while (size > 0 && !m_data[size - 1]) {
            --size;
        }
        m_size = size > skip ? size - skip : 0;
        m_skip = skip;
        computeHash();
        return m_size > 0;
    }

    void fillTestData(uintptr_t n, uintptr_t leaf)
    {
        assert(n < MAX_SIZE);
        m_data[0] = reinterpret_cast<ip_t>(leaf);
        for (uintptr_t i = 1; i <= n; ++i) {
            m_data[i] = reinterpret_cast<ip_t>(i);
        }

        m_size = static_cast<int>(n + 1);
        m_skip = 0;
        computeHash();
    }

    static void setup();

    static void print();

    /// limit the number of frames captured per backtrace, must be in [1, MAX_SIZE]
    static void setMaxDepth(int depth)
    {
        assert(depth >= 1 && depth <= MAX_SIZE);
        s_maxDepth = depth;
    }

    /// compute a hash for a tail starting at @p skip
    hash_t tailHash(int skip) const
    {
        assert(skip > 0);
        assert(skip <= m_size);
        return {XXH3_128bits(begin() + skip, (m_size - skip) * sizeof(ip_t))};
    }

private:
    void computeHash()
    {
        m_hash.value = XXH3_128bits(begin(), m_size * sizeof(ip_t));
    }

    static int unwind(void** data);

    inline static int s_maxDepth = MAX_SIZE;

private:
    int m_size = 0;
    int m_skip = 0;
    hash_t m_hash = {};
    ip_t m_data[MAX_SIZE];
};

namespace std {
template <>
struct hash<Trace::hash_t>
{
    std::size_t operator()(Trace::hash_t hash) const
    {
        return boost::hash_value(std::make_pair(hash.value.low64, hash.value.high64));
    }
};
}

#endif // TRACE_H
