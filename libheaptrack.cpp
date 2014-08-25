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

/**
 * @file libheaptrack.cpp
 *
 * @brief Collect raw heaptrack data by overloading heap allocation functions.
 */

#include <cstdio>
#include <cstdlib>

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <string>
#include <tuple>
#include <memory>
#include <unordered_set>

#include <boost/algorithm/string/replace.hpp>

#include <dlfcn.h>
#include <link.h>

#include "tracetree.h"

/**
 * uncomment this to get extended debug code for known pointers
 * there are still some malloc functions I'm missing apparently,
 * related to TLS and such I guess
 */
// #define DEBUG_MALLOC_PTRS

using namespace std;

namespace {

using malloc_t = void* (*) (size_t);
using free_t = void (*) (void*);
using realloc_t = void* (*) (void*, size_t);
using calloc_t = void* (*) (size_t, size_t);
using posix_memalign_t = int (*) (void **, size_t, size_t);
using valloc_t = void* (*) (size_t);
using aligned_alloc_t = void* (*) (size_t, size_t);
using dlopen_t = void* (*) (const char*, int);

malloc_t real_malloc = nullptr;
free_t real_free = nullptr;
realloc_t real_realloc = nullptr;
calloc_t real_calloc = nullptr;
posix_memalign_t real_posix_memalign = nullptr;
valloc_t real_valloc = nullptr;
aligned_alloc_t real_aligned_alloc = nullptr;
dlopen_t real_dlopen = nullptr;

// threadsafe stuff
atomic<bool> moduleCacheDirty(true);

struct HandleGuard
{
    HandleGuard()
        : wasLocked(inHandler)
    {
        inHandler = true;
    }

    ~HandleGuard()
    {
        inHandler = wasLocked;
    }

    const bool wasLocked;
    static thread_local bool inHandler;
};

thread_local bool HandleGuard::inHandler = false;

string env(const char* variable)
{
    const char* value = getenv(variable);
    return value ? string(value) : string();
}

struct Module
{
    string fileName;
    uintptr_t addressStart;
    uintptr_t addressEnd;

    bool operator<(const Module& module) const
    {
        return make_tuple(addressStart, addressEnd, fileName) < make_tuple(module.addressStart, module.addressEnd, module.fileName);
    }

    bool operator!=(const Module& module) const
    {
        return make_tuple(addressStart, addressEnd, fileName) != make_tuple(module.addressStart, module.addressEnd, module.fileName);
    }
};

struct Data
{
    Data()
    {
        modules.reserve(32);

        string outputFileName = env("DUMP_HEAPTRACK_OUTPUT");
        if (outputFileName.empty()) {
            // env var might not be set when linked directly into an executable
            outputFileName = "heaptrack.$$";
        } else if (outputFileName == "-" || outputFileName == "stdout") {
            out = stdout;
        } else if (outputFileName == "stderr") {
            out = stderr;
        }

        if (!out) {
            boost::replace_all(outputFileName, "$$", to_string(getpid()));
            out = fopen(outputFileName.c_str(), "w");
        }

        if (!out) {
            fprintf(stderr, "Failed to open output file: %s\n", outputFileName.c_str());
            exit(1);
        }

        // TODO: remember meta data about host application, such as cmdline, date of run, ...

        // cleanup environment to prevent tracing of child apps
        unsetenv("DUMP_HEAPTRACK_OUTPUT");
        unsetenv("LD_PRELOAD");
    }

    ~Data()
    {
        HandleGuard::inHandler = true;
        fclose(out);
    }

    void updateModuleCache()
    {
        // check list of loaded modules for unknown ones
        dl_iterate_phdr(dlopen_notify_callback, this);
        moduleCacheDirty = false;
    }

    /**
     * Mostly copied from vogl's src/libbacktrace/btrace.cpp
     */
    static int dlopen_notify_callback(struct dl_phdr_info *info, size_t /*size*/, void *_data)
    {
        auto data = reinterpret_cast<Data*>(_data);
        auto& modules = data->modules;
        bool isExe = false;
        const char *fileName = info->dlpi_name;
        const int BUF_SIZE = 1024;
        char buf[BUF_SIZE];
        // If we don't have a filename and we haven't added our main exe yet, do it now.
        if (!fileName || !fileName[0]) {
            if (modules.empty()) {
                isExe = true;
                ssize_t ret = readlink("/proc/self/exe", buf, sizeof(buf));
                if ((ret > 0) && (ret < (ssize_t)sizeof(buf))) {
                    buf[ret] = 0;
                    fileName = buf;
                }
            }
            if (!fileName || !fileName[0]) {
                return 0;
            }
        }

        uintptr_t addressStart = 0;
        uintptr_t addressEnd = 0;

        for (int i = 0; i < info->dlpi_phnum; i++) {
            if (info->dlpi_phdr[i].p_type == PT_LOAD) {
                if (addressEnd == 0) {
                    addressStart = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
                    addressEnd = addressStart + info->dlpi_phdr[i].p_memsz;
                } else {
                    uintptr_t addr = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr + info->dlpi_phdr[i].p_memsz;
                    if (addr > addressEnd) {
                        addressEnd = addr;
                    }
                }
            }
        }

        Module module{fileName, addressStart, addressEnd};

        auto it = lower_bound(modules.begin(), modules.end(), module);
        if (it == modules.end() || *it != module) {
            fprintf(data->out, "m %s %d %lx %lx\n", module.fileName.c_str(), isExe,
                                                    module.addressStart, module.addressEnd);
            modules.insert(it, module);
        }

        return 0;
    }

    void handleMalloc(void* ptr, size_t size)
    {
        Trace trace;
        if (!trace.fill()) {
            return;
        }

        size_t index = 0;
        {
            lock_guard<mutex> lock(m_mutex);
            if (moduleCacheDirty) {
                updateModuleCache();
            }
            index = traceTree.index(trace, out);

#ifdef DEBUG_MALLOC_PTRS
            auto it = known.find(ptr);
            assert(it == known.end());
            known.insert(ptr);
#endif
        }
        fprintf(out, "+ %lx %lx %lx\n", size, index, reinterpret_cast<uintptr_t>(ptr));
    }

    void handleFree(void* ptr)
    {
#ifdef DEBUG_MALLOC_PTRS
        lock_guard<mutex> lock(m_mutex);
        auto it = known.find(ptr);
        assert(it != known.end());
        known.erase(it);
#endif

        fprintf(out, "- %lx\n", reinterpret_cast<uintptr_t>(ptr));
    }

    mutex m_mutex;

    vector<Module> modules;
    TraceTree traceTree;
    /**
     * Note: We use the C stdio API here for performance reasons.
     *       Esp. in multi-threaded environments this is much faster
     *       to produce non-per-line-interleaved output.
     */
    FILE* out = nullptr;

#ifdef DEBUG_MALLOC_PTRS
    unordered_set<void*> known;
#endif
};

unique_ptr<Data> data;

template<typename T>
T findReal(const char* name)
{
    auto ret = dlsym(RTLD_NEXT, name);
    if (!ret) {
        fprintf(stderr, "Could not find original function %s\n", name);
        abort();
    }
    return reinterpret_cast<T>(ret);
}

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
    if (data || HandleGuard::inHandler) {
        fprintf(stderr, "initialization recursion detected\n");
        abort();
    }

    HandleGuard guard;

    real_calloc = &dummy_calloc;
    real_calloc = findReal<calloc_t>("calloc");
    real_dlopen = findReal<dlopen_t>("dlopen");
    real_malloc = findReal<malloc_t>("malloc");
    real_free = findReal<free_t>("free");
    real_realloc = findReal<realloc_t>("realloc");
    real_posix_memalign = findReal<posix_memalign_t>("posix_memalign");
    real_valloc = findReal<valloc_t>("valloc");
    real_aligned_alloc = findReal<aligned_alloc_t>("aligned_alloc");

    if (unw_set_caching_policy(unw_local_addr_space, UNW_CACHE_PER_THREAD)) {
        fprintf(stderr, "Failed to enable per-thread libunwind caching.\n");
    }

    data.reset(new Data);
}

}

extern "C" {

/// TODO: memalign, pvalloc, ...?

void* malloc(size_t size)
{
    if (!real_malloc) {
        init();
    }

    void* ret = real_malloc(size);

    if (ret && !HandleGuard::inHandler) {
        HandleGuard guard;
        data->handleMalloc(ret, size);
    }

    return ret;
}

void free(void* ptr)
{
    if (!real_free) {
        init();
    }

    // call handler before handing over the real free implementation
    // to ensure the ptr is not reused in-between and thus the output
    // stays consistent
    if (ptr && !HandleGuard::inHandler) {
        HandleGuard guard;
        data->handleFree(ptr);
    }

    real_free(ptr);
}

void* realloc(void* ptr, size_t size)
{
    if (!real_realloc) {
        init();
    }

    void* ret = real_realloc(ptr, size);

    if (ret && !HandleGuard::inHandler) {
        HandleGuard guard;
        if (ptr) {
            data->handleFree(ptr);
        }
        data->handleMalloc(ret, size);
    }

    return ret;
}

void* calloc(size_t num, size_t size)
{
    if (!real_calloc) {
        init();
    }

    void* ret = real_calloc(num, size);

    if (ret && !HandleGuard::inHandler) {
        HandleGuard guard;
        data->handleMalloc(ret, num*size);
    }

    return ret;
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    if (!real_posix_memalign) {
        init();
    }

    int ret = real_posix_memalign(memptr, alignment, size);

    if (ret && !HandleGuard::inHandler) {
        HandleGuard guard;
        data->handleMalloc(*memptr, size);
    }

    return ret;
}

void* aligned_alloc(size_t alignment, size_t size)
{
    if (!real_aligned_alloc) {
        init();
    }

    void* ret = real_aligned_alloc(alignment, size);

    if (ret && !HandleGuard::inHandler) {
        HandleGuard guard;
        data->handleMalloc(ret, size);
    }

    return ret;
}

void* valloc(size_t size)
{
    if (!real_valloc) {
        init();
    }

    void* ret = real_valloc(size);

    if (ret && !HandleGuard::inHandler) {
        HandleGuard guard;
        data->handleMalloc(ret, size);
    }

    return ret;
}

void *dlopen(const char *filename, int flag)
{
    if (!real_dlopen) {
        init();
    }

    void* ret = real_dlopen(filename, flag);

    if (ret) {
        moduleCacheDirty = true;
    }

    return ret;
}

}
