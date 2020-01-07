/*
 * Copyright 2016-2017 Milian Wolff <mail@milianw.de>
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

#include <iostream>
#include <malloc.h>
#include <unistd.h>

#include <benchutil.h>

using namespace std;

int main()
{
    const auto log2_max = 17;
    const auto max_steps = log2_max * 2 + 1;
    unsigned int cost[max_steps];
    int sizes[max_steps];

    const auto baseline = mallinfo().uordblks;

    for (int i = 1; i < max_steps; ++i) {
        int size = 1 << i / 2;
        if (i % 2) {
            size += size / 2;
        }
        sizes[i] = size;
        auto ptr = malloc(size);
        escape(ptr); // prevent the compiler from optimizing the malloc away
        cost[i] = mallinfo().uordblks;
        free(ptr);
    }

    cout << "requested\t|\tactual\t|\toverhead\n";
    for (int i = 1; i < max_steps; ++i) {
        const auto actual = (cost[i] - baseline);
        cout << sizes[i] << "\t\t|\t" << actual << "\t|\t" << (actual - sizes[i]) << '\n';
    }
    return 0;
}
