/*
 * Copyright 2014-2017 Milian Wolff <mail@milianw.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <cstdio>
#include <cstdlib>

#include "util/config.h"

#if defined(_ISOC11_SOURCE)
#define HAVE_ALIGNED_ALLOC 1
#else
#define HAVE_ALIGNED_ALLOC 0
#endif

struct Foo
{
    Foo()
        : i(new int)
    {
    }
    ~Foo()
    {
        delete i;
    }
    int* i;
};

void asdf()
{
    int* i = new int;
    printf("i in asdf: %p\n", (void*)i);
}

void bar()
{
    asdf();
}

void laaa()
{
    bar();
}

void split()
{
    Foo f;
    asdf();
    bar();
    laaa();
}

static Foo foo;

int main()
{
    Foo* f = new Foo;
    printf("new Foo: %p\n", (void*)f);
    delete f;

    char* c = new char[1000];
    printf("new char[]: %p\n", (void*)c);
    delete[] c;

    void* buf = malloc(100);
    printf("malloc: %p\n", buf);
    buf = realloc(buf, 200);
    printf("realloc: %p\n", buf);
    free(buf);

    buf = calloc(5, 5);
    printf("calloc: %p\n", buf);
#if HAVE_CFREE
    cfree(buf);
#else
    free(buf);
#endif

#if HAVE_ALIGNED_ALLOC
    buf = aligned_alloc(16, 160);
    printf("aligned_alloc: %p\n", buf);
    free(buf);
#endif

    buf = valloc(32);
    printf("valloc: %p\n", buf);
    free(buf);

    int __attribute__((unused)) ret = posix_memalign(&buf, 16, 64);
    printf("posix_memalign: %p\n", buf);
    free(buf);

    for (int i = 0; i < 10; ++i) {
        laaa();
    }
    laaa();

    split();

    return 0;
}
