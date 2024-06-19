/*
    SPDX-FileCopyrightText: 2014-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "lib.h"

int main()
{
    std::size_t sum = 0;
    for (int j = 0; j < 10; ++j) {
        Foo foo;
        for (int i = 0; i < 10000; ++i) {
            sum += foo.doBar();
        }
        for (int i = 0; i < 10000; ++i) {
            sum -= foo.doFoo();
        }
    }
    return static_cast<int>(sum);
}
