/*
 * Copyright 2016-2017 Milian Wolff <mail@milianw.de>
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

/**
 * @file heaptrack_api.h
 *
 * This file defines a public API for heaptrack to be used in applications and
 * libraries that implement custom allocators that do not use malloc internally.
 *
 * It should be enough to include this header in your code and then add calls
 * to @c heaptrack_report_alloc, @c heaptrack_report_realloc and
 * @c heaptrack_report_free to your code. By default, nothing will happen.
 * Once you run your code within heaptrack though, this information will be
 * picked up and included in the heap profile data.
 *
 * Note: If you use static linking, or have a custom allocator in your main
 * executable, then you must define HEAPTRACK_API_DLSYM before including
 * this header and link against libdl to make this work properly. The other,
 * more common, case of pool allocators in shared libraries will work with the
 * default implementation that relies on weak symbols and the dynamic linker
 * on resolving the symbols for us directly.
 */

#ifndef HEAPTRACK_API_H
#define HEAPTRACK_API_H

#include <stdlib.h>

#ifndef HEAPTRACK_API_DLSYM

/**
 * By default we rely on weak symbols that get resolved by the dynamic linker.
 * The weak symbols defined here are usually zero, but become non-zero when
 * we run the application with libheaptrack_preload.so loaded in.
 *
 * Note that this does not support run-time attaching yet.
 * Also note that this won't work inside your main executable, nor when you use
 * static linking. In these cases, the dlsym code below should be used instead.
 */

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((weak)) void heaptrack_malloc(void* ptr, size_t size);
__attribute__((weak)) void heaptrack_realloc(void* ptr_in, size_t size, void* ptr_out);
__attribute__((weak)) void heaptrack_free(void* ptr);

#ifdef __cplusplus
}
#endif

#define heaptrack_report_alloc(ptr, size)                                                                              \
    if (heaptrack_malloc)                                                                                              \
    heaptrack_malloc(ptr, size)

#define heaptrack_report_realloc(ptr_in, size, ptr_out)                                                                \
    if (heaptrack_realloc)                                                                                             \
    heaptrack_realloc(ptr_in, size, ptr_out)

#define heaptrack_report_free(ptr)                                                                                     \
    if (heaptrack_free)                                                                                                \
    heaptrack_free(ptr)

#else // HEAPTRACK_API_DLSYM

/**
 * Alternatively, we rely on dlsym to dynamically resolve the symbol to the
 * heaptrack API functions at runtime. This works reliably, also when using
 * static linking or when calling these functions from within your main
 * executable. The caveat is that you need to link to libdl dynamically.
 *
 * @code
 *
 *   gcc -DHEAPTRACK_API_DLSYM=1 -ldl ...
 * @endcode
 */

#ifndef __USE_GNU
// required for RTLD_NEXT
#define __USE_GNU
#endif

#include <dlfcn.h>

struct heaptrack_api_t
{
    void (*malloc)(void*, size_t);
    void (*free)(void*);
    void (*realloc)(void*, size_t, void*);
};
static struct heaptrack_api_t heaptrack_api = {0, 0, 0};

void heaptrack_init_api()
{
    static int initialized = 0;
    if (!initialized) {
        void* sym = dlsym(RTLD_NEXT, "heaptrack_malloc");
        if (sym)
            heaptrack_api.malloc = (void (*)(void*, size_t))sym;

        sym = dlsym(RTLD_NEXT, "heaptrack_realloc");
        if (sym)
            heaptrack_api.realloc = (void (*)(void*, size_t, void*))sym;

        sym = dlsym(RTLD_NEXT, "heaptrack_free");
        if (sym)
            heaptrack_api.free = (void (*)(void*))sym;

        initialized = 1;
    }
}

#define heaptrack_report_alloc(ptr, size)                                                                              \
    do {                                                                                                               \
        heaptrack_init_api();                                                                                          \
        if (heaptrack_api.malloc)                                                                                      \
            heaptrack_api.malloc(ptr, size);                                                                           \
    } while (0)

#define heaptrack_report_realloc(ptr_in, size, ptr_out)                                                                \
    do {                                                                                                               \
        heaptrack_init_api();                                                                                          \
        if (heaptrack_api.realloc)                                                                                     \
            heaptrack_api.realloc(ptr_in, size, ptr_out);                                                              \
    } while (0)

#define heaptrack_report_free(ptr)                                                                                     \
    do {                                                                                                               \
        heaptrack_init_api();                                                                                          \
        if (heaptrack_api.free)                                                                                        \
            heaptrack_api.free(ptr);                                                                                   \
    } while (0)

#endif // HEAPTRACK_API_DLSYM

/**
 * Optionally, you can let heaptrack mimick the Valgrind pool-allocator API.
 *
 * This won't work nicely when you want to enable both, Valgrind and heaptrack.
 * Otherwise, it's an easy way to make your code ready for both tools.
 */
#ifdef HEAPTRACK_DEFINE_VALGRIND_MACROS

#define VALGRIND_DISABLE_ERROR_REPORTING
#define VALGRIND_ENABLE_ERROR_REPORTING
#define VALGRIND_CREATE_MEMPOOL(...)
#define VALGRIND_DESTROY_MEMPOOL(...)
#define VALGRIND_MAKE_MEM_NOACCESS(...)

#define VALGRIND_MEMPOOL_ALLOC(pool, ptr, size) heaptrack_report_alloc(ptr, size)
#define VALGRIND_MEMPOOL_FREE(pool, ptr) heaptrack_report_free(ptr)

#endif

#endif // HEAPTRACK_API_H
