/*
    SPDX-FileCopyrightText: 2014-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef LIB_H
#define LIB_H

#include <cstddef>

class Foo
{
public:
    Foo();
    ~Foo();

    std::size_t doBar();
    std::size_t doFoo();

private:
    class Private;
    Private* d;
};

#endif // LIB_H
