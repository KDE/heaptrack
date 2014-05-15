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
#include <libunwind.h>

namespace {

using malloc_t = void* (*) (size_t);
using free_t = void (*) (void*);

malloc_t real_malloc = nullptr;
free_t real_free = nullptr;

struct InitializeMallocTrace
{
    InitializeMallocTrace()
    {
        real_malloc = reinterpret_cast<malloc_t>(dlsym(RTLD_NEXT, "malloc"));
        real_free = reinterpret_cast<free_t>(dlsym(RTLD_NEXT, "free"));
    }

    ~InitializeMallocTrace()
    {
        real_malloc = 0;
        real_free = nullptr;
    }
};

static InitializeMallocTrace initializeMallocTrace;

void show_backtrace()
{
    const size_t BUFSIZE = 256;
    char name[BUFSIZE];
    unw_cursor_t cursor; unw_context_t uc;
    unw_word_t ip, sp, offp;

    unw_getcontext (&uc);
    unw_init_local (&cursor, &uc);

    while (unw_step(&cursor) > 0)
    {
        name[0] = '\0';
        unw_get_proc_name(&cursor, name, BUFSIZE, &offp);
        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        unw_get_reg(&cursor, UNW_REG_SP, &sp);

        if (name[0] == '\0') {
            strcpy(name, "??");
        }

        printf("%s ip = %lx, sp = %lx\n", name, (long) ip, (long) sp);
        if (!strcmp(name, "main")) {
            break;
        }
    }
}

}

extern "C" {

void* malloc(size_t size)
{
    assert(real_malloc);
    void* ret = real_malloc(size);
    show_backtrace();
    return ret;
}

void free(void* ptr)
{
    assert(real_free);
    real_free(ptr);
}

}
