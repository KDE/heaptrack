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

#include <unordered_map>

#include "bench_pointers.h"
#include "indices.h"

struct PointerHashMap
{
    PointerHashMap()
    {
        map.reserve(65536);
    }

    void addPointer(const uint64_t ptr, const AllocationIndex index)
    {
        map[ptr] = index;
    }

    std::pair<AllocationIndex, bool> takePointer(const uint64_t ptr)
    {
        auto it = map.find(ptr);
        if (it == map.end()) {
            return {{}, false};
        }
        auto ret = std::make_pair(it->second, true);
        map.erase(it);
        return ret;
    }

    std::unordered_map<uint64_t, AllocationIndex> map;
};

int main()
{
    benchPointers<PointerHashMap>();
    return 0;
}