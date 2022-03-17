/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
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
        const auto baseline = mallinfo2().uordblks;
        std::cerr << "allocated vector:        \t" << baseline << std::endl;
        for (uint32_t i = 0; i < NUM_POINTERS; ++i) {
            pointers[i] = reinterpret_cast<uint64_t>(malloc(1));
        }
        const auto allocated = (mallinfo2().uordblks - baseline);
        std::cerr << "allocated input pointers:\t" << allocated << std::endl;
        for (auto ptr : pointers) {
            free(reinterpret_cast<void*>(ptr));
        }
        std::cerr << "freed input pointers:    \t" << (mallinfo2().uordblks - baseline) << std::endl;
        srand(0);
        std::random_shuffle(pointers.begin(), pointers.end());
        malloc_trim(0);
        std::cerr << "begin actual benchmark:  \t" << (mallinfo2().uordblks - baseline) << std::endl;

        {
            Map map;
            for (auto ptr : pointers) {
                AllocationInfoIndex index;
                index.index = static_cast<uint32_t>(ptr);
                map.addPointer(ptr, index);
            }

            const auto added = mallinfo2().uordblks - baseline;
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

            std::cerr << "pointers removed:        \t" << mallinfo2().uordblks << std::endl;
            malloc_trim(0);
            std::cerr << "trimmed:                 \t" << mallinfo2().uordblks << std::endl;
        }
    }
    if (matches != NUM_POINTERS) {
        std::cerr << "FAILED!";
        abort();
    }
}

#endif
