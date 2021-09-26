/*
    SPDX-FileCopyrightText: 2017 Maxim Golov <maxim.golov@gmail.com>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <time.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    time_t t = time(0);
    struct tm tmbuf;
    gmtime_r(&t, &tmbuf);
    char buf[16];
    strftime(buf, sizeof(buf), "%d", &tmbuf);
}
