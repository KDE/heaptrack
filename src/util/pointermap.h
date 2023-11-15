/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef POINTERMAP_H
#define POINTERMAP_H

#include <algorithm>
#include <iostream>
#include <limits>
#include <vector>

#include <boost/functional/hash/hash.hpp>
#include <tsl/robin_map.h>
#include <tsl/robin_set.h>

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
        // allocationInfoIndex not hashed to allow to look it up
        return boost::hash_value(std::tie(info.size, info.traceIndex.index));
    }
};
}

struct AllocationInfoSet
{
    AllocationInfoSet()
    {
        set.reserve(625000);
    }

    bool add(uint64_t size, TraceIndex traceIndex, AllocationInfoIndex* allocationIndex)
    {
        allocationIndex->index = set.size();
        IndexedAllocationInfo info = {size, traceIndex, *allocationIndex};
        auto it = set.find(info);
        if (it != set.end()) {
            *allocationIndex = it->allocationIndex;
            return false;
        } else {
            set.insert(it, info);
            return true;
        }
    }
    tsl::robin_set<IndexedAllocationInfo> set;
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
        auto& indices = mapIt.value();
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
        auto& indices = mapIt.value();
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

private:
    struct Indices
    {
        std::vector<uint16_t> smallPtrParts;
        std::vector<AllocationInfoIndex> allocationIndices;
    };
    tsl::robin_map<uint64_t, Indices> map;
};

#endif // POINTERMAP_H
