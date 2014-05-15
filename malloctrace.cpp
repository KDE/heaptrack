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


const size_t BUF_SIZE = 256;
struct Frame {
    char name[BUF_SIZE];
    unw_word_t offset;
    bool skip;
};

thread_local std::unordered_map<unw_word_t, Frame> frames;
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

        auto it = frames.find(ip);
        if (it == frames.end()) {

            Frame frame;
            frame.name[0] = '\0';
            unw_get_proc_name(&cursor, frame.name, BUF_SIZE, &frame.offset);
            // skip operator new (_Znwm) and operator new[] (_Znam)
            frame.skip = frame.name[0] == '_' && frame.name[1] == 'Z' && frame.name[2] == 'n'
                        && frame.name[4] == 'm' && frame.name[5] == '\0'
                        && (frame.name[3] == 'w' || frame.name[3] == 'a');

            it = frames.insert(it, std::make_pair(ip, frame));
        }

        const Frame& frame = it->second;
        if (!frame.skip) {
            printf("%s+0x%lx@0x%lx %ld\n", frame.name, frame.offset, ip, size);
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
        real_free = reinterpret_cast<free_t>(dlsym(RTLD_NEXT, "free"));
        if (!real_free) {
            fprintf(stderr, "could not find original free\n");
            exit(1);
        }
    }
    real_free(ptr);

    // TODO: actually handle this
}

}
