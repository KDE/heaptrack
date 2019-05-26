/*
 * Copyright 2015-2017 Milian Wolff <mail@milianw.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
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
unsigned int qHash(const Index<Base> index, unsigned int seed = 0) noexcept
{
    return qHash(index.index, seed);
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
struct AllocationInfoIndex : public Index<AllocationInfoIndex>
{
};

struct IndexHasher
{
    template <typename Base>
    std::size_t operator()(const Index<Base> index) const
    {
        return std::hash<uint32_t>()(index.index);
    }
};

namespace std {
template <>
struct hash<TraceIndex> : IndexHasher
{
};
}

#endif // INDICES_H
