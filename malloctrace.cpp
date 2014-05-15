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
#include <atomic>

#include <dlfcn.h>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

namespace {

using malloc_t = void* (*) (size_t);
using free_t = void (*) (void*);
using realloc_t = void* (*) (void*, size_t);

malloc_t real_malloc = nullptr;
free_t real_free = nullptr;
realloc_t real_realloc = nullptr;

struct IPCacheEntry
{
    size_t id;
    bool skip;
    bool stop;
};

// must be kept separately from ThreadData to ensure it stays valid
// even until after ThreadData is destroyed
thread_local bool in_handler = false;

struct ThreadData
{
    ThreadData()
    {
        bool wasInHandler = in_handler;
        in_handler = true;
        ipCache.reserve(1024);
        in_handler = wasInHandler;
    }

    ~ThreadData()
    {
        in_handler = true;
    }

    std::unordered_map<unw_word_t, IPCacheEntry> ipCache;
};

thread_local ThreadData threadData;

std::atomic<size_t> next_id;

void print_caller()
{
    unw_context_t uc;
    unw_getcontext (&uc);

    unw_cursor_t cursor;
    unw_init_local (&cursor, &uc);

    // skip malloc
    if (unw_step(&cursor) <= 0) {
        return;
    }

    auto& ipCache = threadData.ipCache;

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
            const bool stop = !skip && (!strcmp(name, "main") || !strcmp(name, "_GLOBAL__sub_I_main"));
            const size_t id = next_id++;

            // and store it in the cache
            ipCache.insert(it, std::make_pair(ip, IPCacheEntry{next_id, skip, stop}));

            if (!skip) {
                printf("%lu=%lx@%s+0x%lx;", id, ip, name, offset);
            }
            if (stop) {
                break;
            }
            continue;
        }

        const auto& frame = it->second;
        if (!frame.skip) {
            printf("%lu;", frame.id);
        }
        if (frame.stop) {
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
    real_realloc = findReal<realloc_t>("realloc");

    in_handler = false;
}

void handleMalloc(void* ptr, size_t size)
{
    printf("+%ld:%p ", size, ptr);
    print_caller();
    printf("\n");
}

void handleFree(void* ptr)
{
    printf("-%p\n", ptr);
}

}

extern "C" {

/// TODO: calloc, memalign, ...

void* malloc(size_t size)
{
    if (!real_malloc) {
        init();
    }

    void* ret = real_malloc(size);

    if (!in_handler) {
        in_handler = true;
        handleMalloc(ret, size);
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

    if (!in_handler) {
        in_handler = true;
        handleFree(ptr);
        in_handler = false;
    }
}

void* realloc(void* ptr, size_t size)
{
    if (!real_realloc) {
        init();
    }

    void* ret = real_realloc(ptr, size);

    if (!in_handler) {
        in_handler = true;
        handleFree(ptr);
        handleMalloc(ret, size);
        in_handler = false;
    }

    return ret;
}

}
