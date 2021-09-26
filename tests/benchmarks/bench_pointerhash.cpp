/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <tsl/robin_map.h>

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

    tsl::robin_map<uint64_t, AllocationInfoIndex> map;
};

int main()
{
    benchPointers<PointerHashMap>();
    return 0;
}
