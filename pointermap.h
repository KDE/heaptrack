/*
 * Copyright 2015 Milian Wolff <mail@milianw.de>
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

#ifndef POINTERMAP_H
#define POINTERMAP_H

#include <vector>
#include <unordered_map>
#include <map>
#include <limits>
#include <iostream>
#include <algorithm>

#include "indices.h"

/**
 * A low-memory-overhead map of 64bit pointer addresses to 32bit allocation indices.
 *
 * We leverage the fact that pointers are allocated in pages, i.e. close to each
 * other. We split the 64bit address into a common large part and an individual
 * 16bit small part by dividing the address by some number (PageSize below) and
 * keeping the result as the big part and the residue as the small part.
 *
 * The big part of the address is used for a hash map to lookup the Indices
 * structure where we aggregate common pointers in two memory-efficient vectors,
 * one for the 16bit small pointer pairs, and one for the 32bit allocation indices.
 */
class PointerMap
{
    struct SplitPointer
    {
        enum {
            PageSize = std::numeric_limits<uint16_t>::max() / 4
        };
        SplitPointer(uint64_t ptr)
            : big(ptr / PageSize)
            , small(ptr % PageSize)
        {}
        uint64_t big;
        uint16_t small;
    };

public:
    PointerMap()
    {
        map.reserve(1024);
    }

    void addPointer(const uint64_t ptr, const AllocationIndex allocationIndex)
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

    std::pair<AllocationIndex, bool> takePointer(const uint64_t ptr)
    {
        const SplitPointer pointer(ptr);

        auto mapIt = map.find(pointer.big);
        if (mapIt == map.end()) {
            return {{}, false};
        }
        auto& indices = mapIt->second;
        auto pageIt = std::lower_bound(indices.smallPtrParts.begin(), indices.smallPtrParts.end(), pointer.small);
        if (pageIt == indices.smallPtrParts.end() || *pageIt != pointer.small) {
            return  {{}, false};
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
        std::vector<AllocationIndex> allocationIndices;
    };
    std::unordered_map<uint64_t, Indices> map;
};

#endif // POINTERMAP_H
