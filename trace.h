/*
 * Copyright 2014 Milian Wolff <mail@milianw.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef TRACE_H
#define TRACE_H

#include <cstring>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

struct Trace
{
    using ip_t = void*;

    static const int MAX_SIZE = 64;

    const ip_t* begin() const
    {
        return m_data + m_skip;
    }

    const ip_t* end() const
    {
        return begin() + m_size;
    }

    ip_t operator[] (int i) const
    {
        return m_data[m_skip + i];
    }

    int size() const
    {
        return m_size;
    }

    bool fill(int skip = 2)
    {
        int size = unw_backtrace(m_data, MAX_SIZE);
        m_size = size > skip ? size - skip : 0;
        m_skip = skip;
        return m_size > 0;
    }

private:
    int m_size = 0;
    int m_skip = 0;
    ip_t m_data[MAX_SIZE];
};

#endif // TRACE_H
