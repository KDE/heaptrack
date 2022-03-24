/*
    SPDX-FileCopyrightText: 2014-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "libheaptrack.h"
#include "util/config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <type_traits>

using namespace std;

#if defined(_ISOC11_SOURCE)
#define HAVE_ALIGNED_ALLOC 1
#else
#define HAVE_ALIGNED_ALLOC 0
#endif

// NOTE: adding noexcept to C functions is a hard error in clang++
//       (but not even a warning in GCC, even with -Wall)
#if defined(__GNUC__) && !defined(__clang__)
#define LIBC_FUN_ATTRS noexcept
#else
#define LIBC_FUN_ATTRS
#endif

extern "C" {

// Foward declare mimalloc (https://github.com/microsoft/mimalloc) functions so we don't need to include its .h.
void* mi_malloc(size_t size) LIBC_FUN_ATTRS;
void* mi_calloc(size_t count, size_t size) LIBC_FUN_ATTRS;
void* mi_realloc(void* p, size_t newsize) LIBC_FUN_ATTRS;
void  mi_free(void* p) LIBC_FUN_ATTRS;

}

namespace {

namespace hooks {

enum class HookType
{
    Required,
    Optional
};

template <typename Signature, typename Base, HookType Type>
struct hook
{
    Signature original = nullptr;

    void init() noexcept
    {
        auto ret = dlsym(RTLD_NEXT, Base::identifier);
        if (!ret && Type == HookType::Optional) {
            return;
        }
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

#define HOOK(name, type)                                                                                               \
    struct name##_t : public hook<decltype(&::name), name##_t, type>                                                   \
    {                                                                                                                  \
        static constexpr const char* identifier = #name;                                                               \
    } name

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"

HOOK(malloc, HookType::Required);
HOOK(free, HookType::Required);
HOOK(calloc, HookType::Required);
#if HAVE_CFREE
HOOK(cfree, HookType::Optional);
#endif
HOOK(realloc, HookType::Required);
HOOK(posix_memalign, HookType::Optional);
#if HAVE_VALLOC
HOOK(valloc, HookType::Optional);
#endif
#if HAVE_ALIGNED_ALLOC
HOOK(aligned_alloc, HookType::Optional);
#endif
HOOK(dlopen, HookType::Required);
HOOK(dlclose, HookType::Required);

// mimalloc functions
HOOK(mi_malloc, HookType::Optional);
HOOK(mi_calloc, HookType::Optional);
HOOK(mi_realloc, HookType::Optional);
HOOK(mi_free, HookType::Optional);

#pragma GCC diagnostic pop
#undef HOOK

/**
 * Dummy implementation, since the call to dlsym from findReal triggers a call
 * to calloc.
 *
 * This is only called at startup and will eventually be replaced by the
 * "proper" calloc implementation.
 */
struct DummyPool
{
    static const constexpr size_t MAX_SIZE = 1024;
    char buf[MAX_SIZE] = {0};
    size_t offset = 0;

    bool isDummyAllocation(void* ptr) noexcept
    {
        return ptr >= buf && ptr < buf + MAX_SIZE;
    }

    void* alloc(size_t num, size_t size) noexcept
    {
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
};

DummyPool& dummyPool()
{
    static DummyPool pool;
    return pool;
}

void* dummy_calloc(size_t num, size_t size) noexcept
{
    return dummyPool().alloc(num, size);
}

void init()
{
    // heaptrack_init itself calls calloc via std::mutex/_libpthread_init on FreeBSD
    hooks::calloc.original = &dummy_calloc;
    hooks::calloc.init();
    heaptrack_init(getenv("DUMP_HEAPTRACK_OUTPUT"),
                   [] {
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
#if HAVE_VALLOC
                       hooks::valloc.init();
#endif
#if HAVE_ALIGNED_ALLOC
                       hooks::aligned_alloc.init();
#endif

                       // mimalloc functions
                       hooks::mi_malloc.init();
                       hooks::mi_calloc.init();
                       hooks::mi_realloc.init();
                       hooks::mi_free.init();

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

void* malloc(size_t size) LIBC_FUN_ATTRS
{
    if (!hooks::malloc) {
        hooks::init();
    }

    void* ptr = hooks::malloc(size);
    heaptrack_malloc(ptr, size);
    return ptr;
}

void free(void* ptr) LIBC_FUN_ATTRS
{
    if (!hooks::free) {
        hooks::init();
    }

    if (hooks::dummyPool().isDummyAllocation(ptr)) {
        return;
    }

    // call handler before handing over the real free implementation
    // to ensure the ptr is not reused in-between and thus the output
    // stays consistent
    heaptrack_free(ptr);

    hooks::free(ptr);
}

void* realloc(void* ptr, size_t size) LIBC_FUN_ATTRS
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

void* calloc(size_t num, size_t size) LIBC_FUN_ATTRS
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
void cfree(void* ptr) LIBC_FUN_ATTRS
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

int posix_memalign(void** memptr, size_t alignment, size_t size) LIBC_FUN_ATTRS
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
void* aligned_alloc(size_t alignment, size_t size) LIBC_FUN_ATTRS
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

#if HAVE_VALLOC
void* valloc(size_t size) LIBC_FUN_ATTRS
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
#endif

void* dlopen(const char* filename, int flag) LIBC_FUN_ATTRS
{
    if (!hooks::dlopen) {
        hooks::init();
    }

#ifdef RTLD_DEEPBIND
    if (filename && flag & RTLD_DEEPBIND) {
        heaptrack_warning([](FILE* out) {
            fprintf(out,
                    "Detected dlopen call with RTLD_DEEPBIND which breaks function call interception. "
                    "Heaptrack will drop this flag. If your application relies on it, try to run `heaptrack "
                    "--use-inject` instead.");
        });
        flag &= ~RTLD_DEEPBIND;
    }
#endif

    void* ret = hooks::dlopen(filename, flag);

    if (ret) {
        heaptrack_invalidate_module_cache();
    }

    return ret;
}

int dlclose(void* handle) LIBC_FUN_ATTRS
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

// mimalloc functions, implementations just copied from above and names changed
void* mi_malloc(size_t size) LIBC_FUN_ATTRS
{
    if (!hooks::mi_malloc) {
        hooks::init();
    }

    void* ptr = hooks::mi_malloc(size);
    heaptrack_malloc(ptr, size);
    return ptr;
}

void* mi_realloc(void* ptr, size_t size) LIBC_FUN_ATTRS
{
    if (!hooks::mi_realloc) {
        hooks::init();
    }

    void* ret = hooks::mi_realloc(ptr, size);

    if (ret) {
        heaptrack_realloc(ptr, size, ret);
    }

    return ret;
}

void* mi_calloc(size_t num, size_t size) LIBC_FUN_ATTRS
{
    if (!hooks::mi_calloc) {
        hooks::init();
    }

    void* ret = hooks::mi_calloc(num, size);

    if (ret) {
        heaptrack_malloc(ret, num * size);
    }

    return ret;
}

void mi_free(void* ptr) LIBC_FUN_ATTRS
{
    if (!hooks::mi_free) {
        hooks::init();
    }

    if (hooks::dummyPool().isDummyAllocation(ptr)) {
        return;
    }

    // call handler before handing over the real free implementation
    // to ensure the ptr is not reused in-between and thus the output
    // stays consistent
    heaptrack_free(ptr);

    hooks::mi_free(ptr);
}
}
