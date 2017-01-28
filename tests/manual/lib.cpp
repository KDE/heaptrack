/*
 * Copyright 2014-2017 Milian Wolff <mail@milianw.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
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
