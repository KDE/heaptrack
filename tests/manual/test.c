/*
    SPDX-FileCopyrightText: 2014-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "../benchutil.h"

int main()
{
    int i;
    // make app deterministic
    srand(0);
    void* p = malloc(1);
    for (i = 0; i < 10000; ++i) {
        void* l = malloc(rand() % 1000);
        escape(l);
        usleep(100);
    }
    printf("malloc: %p\n", p);
    free(p);
    return 0;
}
