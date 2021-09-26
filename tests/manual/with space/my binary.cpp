/*
    SPDX-FileCopyrightText: 2021 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <cstdlib>

#include "../../benchutil.h"

int main()
{
    auto p = malloc(42);
    escape(p);
    free(p);
    return 0;
}
