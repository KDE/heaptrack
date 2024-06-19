/*
    SPDX-FileCopyrightText: 2014-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../benchutil.h"

int main()
{
    int i;
    // make app deterministic
    srand(0);
    void* p = malloc(1);
    for (i = 0; i < 10000; ++i) {
        void* l = malloc((size_t)(rand() % 1000));
        escape(l);
        usleep(100);
    }
    printf("malloc: %p\n", p);
    free(p);
    return 0;
}
