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

#include <unordered_map>

#include "bench_pointers.h"
#include "src/util/indices.h"

struct PointerHashMap
{
    PointerHashMap()
    {
        map.reserve(65536);
    }

    void addPointer(const uint64_t ptr, const AllocationInfoIndex index)
    {
        map[ptr] = index;
    }

    std::pair<AllocationInfoIndex, bool> takePointer(const uint64_t ptr)
    {
        auto it = map.find(ptr);
        if (it == map.end()) {
            return {{}, false};
        }
        auto ret = std::make_pair(it->second, true);
        map.erase(it);
        return ret;
    }

    std::unordered_map<uint64_t, AllocationInfoIndex> map;
};

int main()
{
    benchPointers<PointerHashMap>();
    return 0;
}
