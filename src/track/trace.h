/*
    SPDX-FileCopyrightText: 2014-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef TRACE_H
#define TRACE_H

#include <cassert>
#include <cstdint>

/**
 * @brief Backtrace interface.
 */
struct Trace
{
    using ip_t = void*;

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
        return m_size > 0;
    }

    void fillTestData(uintptr_t n, uintptr_t leaf)
    {
        assert(n < MAX_SIZE);
        m_data[0] = reinterpret_cast<ip_t>(leaf);
        for (uintptr_t i = 1; i <= n; ++i) {
            m_data[i] = reinterpret_cast<ip_t>(i);
        }

        m_size = n + 1;
        m_skip = 0;
    }

    static void setup();
    static void invalidateModuleCache();

    static void print();

private:
    static int unwind(void** data);

private:
    int m_size = 0;
    int m_skip = 0;
    ip_t m_data[MAX_SIZE];
};

#endif // TRACE_H
