/*
    SPDX-FileCopyrightText: 2017 Maxim Golov <maxim.golov@gmail.com>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <stdlib.h>
#include <time.h>

int main()
{
    time_t t = time(0);
    struct tm tmbuf;
    gmtime_r(&t, &tmbuf);
    char buf[16];
    strftime(buf, sizeof(buf), "%d", &tmbuf);
}
