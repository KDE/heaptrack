/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef INDICES_H
#define INDICES_H

#include <functional>

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

    void operator++()
    {
        ++index;
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
struct hash<StringIndex> : IndexHasher
{
};
template <>
struct hash<TraceIndex> : IndexHasher
{
};
template <>
struct hash<IpIndex> : IndexHasher
{
};
}

#endif // INDICES_H
