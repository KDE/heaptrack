/*
 * Copyright 2015-2017 Milian Wolff <mail@milianw.de>
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

#ifndef INDICES_H
#define INDICES_H

#include <unordered_map>

// sadly, C++ doesn't yet have opaque typedefs
template <typename Base>
struct Index
{
    uint32_t index = 0;

    explicit operator bool() const
    {
        return index;
    }

    bool operator<(Index o) const
    {
        return index < o.index;
    }

    bool operator<=(Index o) const
    {
        return index <= o.index;
    }

    bool operator>(Index o) const
    {
        return index > o.index;
    }

    bool operator>=(Index o) const
    {
        return index >= o.index;
    }

    bool operator!=(Index o) const
    {
        return index != o.index;
    }

    bool operator==(Index o) const
    {
        return index == o.index;
    }
};

template <typename Base>
uint qHash(const Index<Base> index, uint seed = 0) noexcept
{
    return qHash(index.index, seed);
}

namespace std {
template <typename Base>
struct hash<Index<Base>>
{
    std::size_t operator()(const Index<Base> index) const
    {
        return std::hash<uint32_t>()(index.index);
    }
};
}

struct StringIndex : public Index<StringIndex>
{
};
struct ModuleIndex : public StringIndex
{
};
struct FunctionIndex : public StringIndex
{
};
struct FileIndex : public StringIndex
{
};
struct IpIndex : public Index<IpIndex>
{
};
struct TraceIndex : public Index<TraceIndex>
{
};
struct AllocationIndex : public Index<AllocationIndex>
{
};

#endif // INDICES_H
