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

#include "libheaptrack.h"

#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <type_traits>
#include <atomic>

using namespace std;

namespace {

namespace hooks {

template<typename SignatureT>
struct hook
{
    hook(const char* const name)
        : name(name)
    {}

    using Signature = SignatureT*;
    Signature original = nullptr;

    const char * const name;
    void init()
    {
        auto ret = dlsym(RTLD_NEXT, name);
        if (!ret) {
            fprintf(stderr, "Could not find original function %s\n", name);
            abort();
        }
        original = reinterpret_cast<Signature>(ret);
    }

    template<typename... Args>
    auto operator() (Args... args) const -> decltype(original(args...))
    {
        return original(args...);
    }

    explicit operator bool () const
    {
        return original;
    }
};

hook<decltype(::malloc)> malloc{"malloc"};
hook<decltype(::free)> free{"free"};
hook<decltype(::calloc)> calloc{"calloc"};
hook<decltype(::cfree)> cfree{"cfree"};
hook<decltype(::realloc)> realloc{"realloc"};
hook<decltype(::posix_memalign)> posix_memalign{"posix_memalign"};
hook<decltype(::valloc)> valloc{"valloc"};
hook<decltype(::aligned_alloc)> aligned_alloc{"aligned_alloc"};
hook<decltype(::dlopen)> dlopen{"dlopen"};
hook<decltype(::dlclose)> dlclose{"dlclose"};

/**
 * Dummy implementation, since the call to dlsym from findReal triggers a call to calloc.
 *
 * This is only called at startup and will eventually be replaced by the "proper" calloc implementation.
 */
void* dummy_calloc(size_t num, size_t size)
{
    const size_t MAX_SIZE = 1024;
    static char* buf[MAX_SIZE];
    static size_t offset = 0;
    if (!offset) {
        memset(buf, 0, MAX_SIZE);
    }
    size_t oldOffset = offset;
    offset += num * size;
    if (offset >= MAX_SIZE) {
        fprintf(stderr, "failed to initialize, dummy calloc buf size exhausted: %lu requested, %lu available\n", offset, MAX_SIZE);
        abort();
    }
    return buf + oldOffset;
}

void init()
{
    heaptrack_init(getenv("DUMP_HEAPTRACK_OUTPUT"), [] {
        hooks::calloc.original = &dummy_calloc;
        hooks::calloc.init();
        hooks::dlopen.init();
        hooks::dlclose.init();
        hooks::malloc.init();
        hooks::free.init();
        hooks::calloc.init();
        hooks::cfree.init();
        hooks::realloc.init();
        hooks::posix_memalign.init();
        hooks::valloc.init();
        hooks::aligned_alloc.init();

        // cleanup environment to prevent tracing of child apps
        unsetenv("LD_PRELOAD");
    }, nullptr, nullptr);
}

}

}

extern "C" {

/// TODO: memalign, pvalloc, ...?

void* malloc(size_t size)
{
    if (!hooks::malloc) {
        hooks::init();
    }

    void* ptr = hooks::malloc(size);
    heaptrack_malloc(ptr, size);
    return ptr;
}

void free(void* ptr)
{
    if (!hooks::free) {
        hooks::init();
    }

    // call handler before handing over the real free implementation
    // to ensure the ptr is not reused in-between and thus the output
    // stays consistent
    heaptrack_free(ptr);

    hooks::free(ptr);
}

void* realloc(void* ptr, size_t size)
{
    if (!hooks::realloc) {
        hooks::init();
    }

    void* ret = hooks::realloc(ptr, size);

    if (ret) {
        heaptrack_realloc(ptr, size, ret);
    }

    return ret;
}

void* calloc(size_t num, size_t size)
{
    if (!hooks::calloc) {
        hooks::init();
    }

    void* ret = hooks::calloc(num, size);

    if (ret) {
        heaptrack_malloc(ret, num * size);
    }

    return ret;
}

void cfree(void* ptr)
{
    if (!hooks::cfree) {
        hooks::init();
    }

    // call handler before handing over the real free implementation
    // to ensure the ptr is not reused in-between and thus the output
    // stays consistent
    if (ptr) {
        heaptrack_free(ptr);
    }

    hooks::cfree(ptr);
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    if (!hooks::posix_memalign) {
        hooks::init();
    }

    int ret = hooks::posix_memalign(memptr, alignment, size);

    if (!ret) {
        heaptrack_malloc(*memptr, size);
    }

    return ret;
}

void* aligned_alloc(size_t alignment, size_t size)
{
    if (!hooks::aligned_alloc) {
        hooks::init();
    }

    void* ret = hooks::aligned_alloc(alignment, size);

    if (ret) {
        heaptrack_malloc(ret, size);
    }

    return ret;
}

void* valloc(size_t size)
{
    if (!hooks::valloc) {
        hooks::init();
    }

    void* ret = hooks::valloc(size);

    if (ret) {
        heaptrack_malloc(ret, size);
    }

    return ret;
}

void *dlopen(const char *filename, int flag)
{
    if (!hooks::dlopen) {
        hooks::init();
    }

    void* ret = hooks::dlopen(filename, flag);

    if (ret) {
        heaptrack_invalidate_module_cache();
    }

    return ret;
}

int dlclose(void *handle)
{
    if (!hooks::dlclose) {
        hooks::init();
    }

    int ret = hooks::dlclose(handle);

    if (!ret) {
        heaptrack_invalidate_module_cache();
    }

    return ret;
}

}
