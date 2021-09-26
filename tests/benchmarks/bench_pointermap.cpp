/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "bench_pointers.h"
#include "src/util/pointermap.h"

int main()
{
    benchPointers<PointerMap>();
    return 0;
}
