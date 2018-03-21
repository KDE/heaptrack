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

#include "libheaptrack.h"
#include "util/config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

#include <atomic>
#include <type_traits>

using namespace std;

#if defined(_ISOC11_SOURCE)
#define HAVE_ALIGNED_ALLOC 1
#else
#define HAVE_ALIGNED_ALLOC 0
#endif

extern "C" {
__attribute__((weak)) void __libc_freeres();
}
namespace __gnu_cxx {
__attribute__((weak)) extern void __freeres();
}

namespace {

namespace hooks {

template <typename Signature, typename Base>
struct hook
{
    Signature original = nullptr;

    void init() noexcept
    {
        auto ret = dlsym(RTLD_NEXT, Base::identifier);
        if (!ret) {
            fprintf(stderr, "Could not find original function %s\n", Base::identifier);
            abort();
        }
        original = reinterpret_cast<Signature>(ret);
    }

    template <typename... Args>
    auto operator()(Args... args) const noexcept -> decltype(original(args...))
    {
        return original(args...);
    }

    explicit operator bool() const noexcept
    {
        return original;
    }
};

#define HOOK(name)                                                                                                     \
    struct name##_t : public hook<decltype(&::name), name##_t>                                                         \
    {                                                                                                                  \
        static constexpr const char* identifier = #name;                                                               \
    } name

HOOK(malloc);
HOOK(free);
HOOK(calloc);
#if HAVE_CFREE
HOOK(cfree);
#endif
HOOK(realloc);
HOOK(posix_memalign);
HOOK(valloc);
#if HAVE_ALIGNED_ALLOC
HOOK(aligned_alloc);
#endif
HOOK(dlopen);
HOOK(dlclose);

/**
 * Dummy implementation, since the call to dlsym from findReal triggers a call
 * to calloc.
 *
 * This is only called at startup and will eventually be replaced by the
 * "proper" calloc implementation.
 */
void* dummy_calloc(size_t num, size_t size) noexcept
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
        fprintf(stderr,
                "failed to initialize, dummy calloc buf size exhausted: "
                "%zu requested, %zu available\n",
                offset, MAX_SIZE);
        abort();
    }
    return buf + oldOffset;
}

void init()
{
    atexit([]() {
        // free internal libstdc++ resources
        // see also Valgrind's `--run-cxx-freeres` option
        if (&__gnu_cxx::__freeres) {
            __gnu_cxx::__freeres();
        }
        // free internal libc resources, cf: https://bugs.kde.org/show_bug.cgi?id=378765
        // see also Valgrind's `--run-libc-freeres` option
        if (&__libc_freeres) {
            __libc_freeres();
        }
    });
    heaptrack_init(getenv("DUMP_HEAPTRACK_OUTPUT"),
                   [] {
                       hooks::calloc.original = &dummy_calloc;
                       hooks::calloc.init();
                       hooks::dlopen.init();
                       hooks::dlclose.init();
                       hooks::malloc.init();
                       hooks::free.init();
                       hooks::calloc.init();
#if HAVE_CFREE
                       hooks::cfree.init();
#endif
                       hooks::realloc.init();
                       hooks::posix_memalign.init();
                       hooks::valloc.init();
#if HAVE_ALIGNED_ALLOC
                       hooks::aligned_alloc.init();
#endif

                       // cleanup environment to prevent tracing of child apps
                       unsetenv("LD_PRELOAD");
                       unsetenv("DUMP_HEAPTRACK_OUTPUT");
                   },
                   nullptr, nullptr);
}
}
}

extern "C" {

/// TODO: memalign, pvalloc, ...?

void* malloc(size_t size) noexcept
{
    if (!hooks::malloc) {
        hooks::init();
    }

    void* ptr = hooks::malloc(size);
    heaptrack_malloc(ptr, size);
    return ptr;
}

void free(void* ptr) noexcept
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

void* realloc(void* ptr, size_t size) noexcept
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

void* calloc(size_t num, size_t size) noexcept
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

#if HAVE_CFREE
void cfree(void* ptr) noexcept
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
#endif

int posix_memalign(void** memptr, size_t alignment, size_t size) noexcept
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

#if HAVE_ALIGNED_ALLOC
void* aligned_alloc(size_t alignment, size_t size) noexcept
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
#endif

void* valloc(size_t size) noexcept
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

void* dlopen(const char* filename, int flag) noexcept
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

int dlclose(void* handle) noexcept
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
