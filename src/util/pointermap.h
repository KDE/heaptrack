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

#ifndef POINTERMAP_H
#define POINTERMAP_H

#include <algorithm>
#include <iostream>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/functional/hash.hpp>

#include "indices.h"

/**
 * Information for a single call to an allocation function for big allocations.
 */
struct IndexedAllocationInfo
{
    uint64_t size;
    TraceIndex traceIndex;
    AllocationInfoIndex allocationIndex;
    bool operator==(const IndexedAllocationInfo& rhs) const
    {
        return rhs.traceIndex == traceIndex && rhs.size == size;
        // allocationInfoIndex not compared to allow to look it up
    }
};

namespace std {
template <>
struct hash<IndexedAllocationInfo>
{
    size_t operator()(const IndexedAllocationInfo& info) const
    {
        size_t seed = 0;
        boost::hash_combine(seed, info.size);
        boost::hash_combine(seed, info.traceIndex.index);
        // allocationInfoIndex not hashed to allow to look it up
        return seed;
    }
};
}

//todo change name
struct AllocationInfoSet
{
    AllocationInfoSet()
    {
        m_set.reserve(625000);
    }

    bool add(uint64_t size, TraceIndex traceIndex, AllocationInfoIndex* allocationIndex)
    {
        allocationIndex->index = m_set.size();
        IndexedAllocationInfo info = {size, traceIndex, *allocationIndex};
        auto it = m_set.find(info);
        if (it != m_set.end()) {
            *allocationIndex = it->allocationIndex;
            return false;
        } else {
            auto ret = m_set.insert(it, info);
            m_allocationIndexToInfo.emplace_back(ret);
            return true;
        }
    }

    bool findAllocationInfo(AllocationInfoIndex index, IndexedAllocationInfo& info)
    {
        if(index.index >= m_allocationIndexToInfo.size())
        {
            return false;
        }
        info = *m_allocationIndexToInfo[index.index];
        return true;
    }
private:
    std::unordered_set<IndexedAllocationInfo> m_set;
    std::vector<decltype(m_set)::iterator> m_allocationIndexToInfo;
};

/**
 * A low-memory-overhead map of 64bit pointer addresses to 32bit allocation
 * indices.
 *
 * We leverage the fact that pointers are allocated in pages, i.e. close to each
 * other. We split the 64bit address into a common large part and an individual
 * 16bit small part by dividing the address by some number (PageSize below) and
 * keeping the result as the big part and the residue as the small part.
 *
 * The big part of the address is used for a hash map to lookup the Indices
 * structure where we aggregate common pointers in two memory-efficient vectors,
 * one for the 16bit small pointer pairs, and one for the 32bit allocation
 * indices.
 */
class PointerMap
{
    struct SplitPointer
    {
        enum
        {
            PageSize = std::numeric_limits<uint16_t>::max() / 4
        };
        SplitPointer(uint64_t ptr)
            : big(ptr / PageSize)
            , small(ptr % PageSize)
        {
        }
        uint64_t big;
        uint16_t small;
    };

public:
    PointerMap()
    {
        map.reserve(1024);
    }

    void addPointer(const uint64_t ptr, const AllocationInfoIndex allocationIndex)
    {
        const SplitPointer pointer(ptr);

        auto mapIt = map.find(pointer.big);
        if (mapIt == map.end()) {
            mapIt = map.insert(mapIt, std::make_pair(pointer.big, Indices()));
        }
        auto& indices = mapIt->second;
        auto pageIt = std::lower_bound(indices.smallPtrParts.begin(), indices.smallPtrParts.end(), pointer.small);
        auto allocationIt = indices.allocationIndices.begin() + distance(indices.smallPtrParts.begin(), pageIt);
        if (pageIt == indices.smallPtrParts.end() || *pageIt != pointer.small) {
            indices.smallPtrParts.insert(pageIt, pointer.small);
            indices.allocationIndices.insert(allocationIt, allocationIndex);
        } else {
            *allocationIt = allocationIndex;
        }
    }

    std::pair<AllocationInfoIndex, bool> takePointer(const uint64_t ptr)
    {
        const SplitPointer pointer(ptr);

        auto mapIt = map.find(pointer.big);
        if (mapIt == map.end()) {
            return {{}, false};
        }
        auto& indices = mapIt->second;
        auto pageIt = std::lower_bound(indices.smallPtrParts.begin(), indices.smallPtrParts.end(), pointer.small);
        if (pageIt == indices.smallPtrParts.end() || *pageIt != pointer.small) {
            return {{}, false};
        }
        auto allocationIt = indices.allocationIndices.begin() + distance(indices.smallPtrParts.begin(), pageIt);
        auto index = *allocationIt;
        indices.allocationIndices.erase(allocationIt);
        indices.smallPtrParts.erase(pageIt);
        if (indices.allocationIndices.empty()) {
            map.erase(mapIt);
        }
        return {index, true};
    }

    std::pair<AllocationInfoIndex, uint64_t> takePointer()
    {
        AllocationInfoIndex index;
        if(map.empty())
        {
            return {index, 0};
        }
        auto mapIt = map.begin();
        auto ptr = mapIt->first * SplitPointer::PageSize + mapIt->second.smallPtrParts.back();
        index = mapIt->second.allocationIndices.back();
        mapIt->second.allocationIndices.pop_back();
        mapIt->second.smallPtrParts.pop_back();
        if(mapIt->second.smallPtrParts.empty())
        {
            map.erase(mapIt);
        }
        return {index, ptr};
    }
private:
    struct Indices
    {
        std::vector<uint16_t> smallPtrParts;
        std::vector<AllocationInfoIndex> allocationIndices;
    };
    std::unordered_map<uint64_t, Indices> map;
};

#endif // POINTERMAP_H
