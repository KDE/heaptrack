/*
    SPDX-FileCopyrightText: 2016-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
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
    size_t cost[max_steps];
    int sizes[max_steps];

    const auto baseline = mallinfo2().uordblks;

    for (int i = 1; i < max_steps; ++i) {
        int size = 1 << i / 2;
        if (i % 2) {
            size += size / 2;
        }
        sizes[i] = size;
        auto ptr = malloc(size);
        escape(ptr); // prevent the compiler from optimizing the malloc away
        cost[i] = mallinfo2().uordblks;
        free(ptr);
    }

    cout << "requested\t|\tactual\t|\toverhead\n";
    for (int i = 1; i < max_steps; ++i) {
        const auto actual = (cost[i] - baseline);
        cout << sizes[i] << "\t\t|\t" << actual << "\t|\t" << (actual - sizes[i]) << '\n';
    }
    return 0;
}
