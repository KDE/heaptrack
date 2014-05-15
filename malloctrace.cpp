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

#include <unordered_map>

#include <dlfcn.h>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

namespace {

using malloc_t = void* (*) (size_t);
using free_t = void (*) (void*);

malloc_t real_malloc = nullptr;
free_t real_free = nullptr;

struct IPCacheEntry {
    bool skip;
};

thread_local std::unordered_map<unw_word_t, IPCacheEntry> ipCache;
thread_local bool in_handler;

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

    while (unw_step(&cursor) > 0)
    {
        unw_word_t ip;
        unw_get_reg(&cursor, UNW_REG_IP, &ip);

        // check cache for known ip's
        auto it = ipCache.find(ip);
        if (it == ipCache.end()) {
            // not cached yet, get data
            const size_t BUF_SIZE = 256;
            char name[BUF_SIZE];
            name[0] = '\0';
            unw_word_t offset;
            unw_get_proc_name(&cursor, name, BUF_SIZE, &offset);
            // skip operator new (_Znwm) and operator new[] (_Znam)
            const bool skip = name[0] == '_' && name[1] == 'Z' && name[2] == 'n'
                        && name[4] == 'm' && name[5] == '\0'
                        && (name[3] == 'w' || name[3] == 'a');

            // and store it in the cache
            it = ipCache.insert(it, std::make_pair(ip, IPCacheEntry{skip}));

            if (!skip) {
                printf("=%lx %s+0x%lx\n", ip, name, offset);
            }
        }

        const auto& frame = it->second;
        if (!frame.skip) {
            printf("+%lx %ld\n", ip, size);
            break;
        }
    }
}

template<typename T>
T findReal(const char* name)
{
    auto ret = dlsym(RTLD_NEXT, name);
    if (!ret) {
        fprintf(stderr, "could not find original function %s\n", name);
        exit(1);
    }
    return reinterpret_cast<T>(ret);
}

void init()
{
    if (in_handler) {
        fprintf(stderr, "initialization recursion detected\n");
        exit(1);
    }

    in_handler = true;

    real_malloc = findReal<malloc_t>("malloc");
    real_free = findReal<free_t>("free");

    ipCache.reserve(1024);

    in_handler = false;
}

}

extern "C" {

/// TODO: realloc, calloc, memalign, ...

void* malloc(size_t size)
{
    if (!real_malloc) {
        init();
    }

    void* ret = real_malloc(size);

    if (!in_handler) {
        in_handler = true;
        print_caller(size);
        in_handler = false;
    }

    return ret;
}

void free(void* ptr)
{
    if (!real_free) {
        init();
    }

    real_free(ptr);

    // TODO: actually handle this
}

}
