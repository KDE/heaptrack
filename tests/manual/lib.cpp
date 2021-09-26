/*
    SPDX-FileCopyrightText: 2014-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "lib.h"

#include <vector>

class Foo::Private
{
public:
    std::vector<size_t> data;

    void push()
    {
        data.push_back(data.size());
    }
};

Foo::Foo()
    : d(new Private)
{
}

Foo::~Foo()
{
    delete d;
}

size_t Foo::doBar()
{
    d->push();
    return d->data.size();
}

size_t Foo::doFoo()
{
    size_t ret = d->data.back();
    d->data.pop_back();
    return ret;
}
