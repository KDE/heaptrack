/*
 * Copyright 2014 Milian Wolff <mail@milianw.de>
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

#include <cstdio>
#include <cassert>
#include <cstring>
#include <cstdlib>

#include <dlfcn.h>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

namespace {

using malloc_t = void* (*) (size_t);
using free_t = void (*) (void*);

malloc_t real_malloc = nullptr;
free_t real_free = nullptr;

void print_caller(size_t size)
{
    unw_context_t uc;
    unw_getcontext (&uc);

    unw_cursor_t cursor;
    unw_init_local (&cursor, &uc);

    // skip malloc
    if (unw_step(&cursor) <= 0) {
        return;
    }

    size_t BUF_SIZE = 256;
    char name[BUF_SIZE];
    unw_word_t offset;
    unw_word_t ip;

    printf("-----\n");
    while (unw_step(&cursor) > 0)
    {
        name[0] = '\0';
        unw_get_proc_name(&cursor, name, BUF_SIZE, &offset);
        if (name[0] != '_' || name[1] != 'Z' ||
            (strcmp(name, "_Znwm") && // operator new
            strcmp(name, "_Znam")))   // operator new[]
        {
            unw_get_reg(&cursor, UNW_REG_IP, &ip);
            printf("%s+0x%lx@0x%lx %ld\n", name, offset, ip, size);
            break;
        }
    }
}

}

extern "C" {

void* malloc(size_t size)
{
    if (!real_malloc) {
        real_malloc = reinterpret_cast<malloc_t>(dlsym(RTLD_NEXT, "malloc"));
        if (!real_malloc) {
            fprintf(stderr, "could not find original malloc\n");
            exit(1);
        }
    }
    assert(real_malloc);
    void* ret = real_malloc(size);
    print_caller(size);
    return ret;
}

void free(void* ptr)
{
    if (!real_free) {
        real_free = reinterpret_cast<free_t>(dlsym(RTLD_NEXT, "free"));
        if (!real_free) {
            fprintf(stderr, "could not find original free\n");
            exit(1);
        }
    }
    assert(real_free);
    real_free(ptr);
}

}
