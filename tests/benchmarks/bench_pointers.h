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

#ifndef BENCH_POINTERS
#define BENCH_POINTERS

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

#include <malloc.h>

#include "src/util/indices.h"

template <typename Map>
void benchPointers()
{
    uint32_t matches = 0;
    constexpr uint32_t NUM_POINTERS = 10000000;
    {
        std::vector<uint64_t> pointers(NUM_POINTERS);
        const auto baseline = mallinfo().uordblks;
        std::cerr << "allocated vector:        \t" << baseline << std::endl;
        for (uint32_t i = 0; i < NUM_POINTERS; ++i) {
            pointers[i] = reinterpret_cast<uint64_t>(malloc(1));
        }
        const auto allocated = (mallinfo().uordblks - baseline);
        std::cerr << "allocated input pointers:\t" << allocated << std::endl;
        for (auto ptr : pointers) {
            free(reinterpret_cast<void*>(ptr));
        }
        std::cerr << "freed input pointers:    \t" << (mallinfo().uordblks - baseline) << std::endl;
        srand(0);
        std::random_shuffle(pointers.begin(), pointers.end());
        malloc_trim(0);
        std::cerr << "begin actual benchmark:  \t" << (mallinfo().uordblks - baseline) << std::endl;

        {
            Map map;
            for (auto ptr : pointers) {
                AllocationInfoIndex index;
                index.index = static_cast<uint32_t>(ptr);
                map.addPointer(ptr, index);
            }

            const auto added = mallinfo().uordblks - baseline;
            std::cerr << "pointers added:          \t" << added << " (" << (float(added) * 100.f / allocated)
                      << "% overhead)" << std::endl;

            std::random_shuffle(pointers.begin(), pointers.end());
            for (auto ptr : pointers) {
                AllocationInfoIndex index;
                index.index = static_cast<uint32_t>(ptr);
                auto allocation = map.takePointer(ptr);
                if (allocation.second && allocation.first == index) {
                    ++matches;
                }
            }

            std::cerr << "pointers removed:        \t" << mallinfo().uordblks << std::endl;
            malloc_trim(0);
            std::cerr << "trimmed:                 \t" << mallinfo().uordblks << std::endl;
        }
    }
    if (matches != NUM_POINTERS) {
        std::cerr << "FAILED!";
        abort();
    }
}

#endif
