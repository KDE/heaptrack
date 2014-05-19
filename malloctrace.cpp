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
#include <string>
#include <vector>

#include <boost/functional/hash.hpp>

#include <dlfcn.h>
#include <unistd.h>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include "libbacktrace/btrace.h"

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

struct IPCacheEntry
{
    unsigned int id;
    bool stop;
};

atomic<unsigned int> next_ipCache_id;
atomic<unsigned int> next_traceCache_id;
atomic<unsigned int> next_thread_id;
atomic<unsigned int> next_string_id(1);

// must be kept separately from ThreadData to ensure it stays valid
// even until after ThreadData is destroyed
thread_local bool in_handler = false;

string env(const char* variable)
{
    const char* value = getenv(variable);
    return value ? string(value) : string();
}

struct Trace
{
    static const unsigned int MAX_DEPTH = 64;
    unsigned int data[MAX_DEPTH];
    unsigned int depth;

    bool operator==(const Trace& o) const
    {
        return depth == o.depth && !memcmp(data, o.data, sizeof(unsigned int) * depth);
    }
};

struct TraceHasher
{
    size_t operator()(const Trace& trace) const
    {
        size_t seed = 0;
        hash<unsigned int> nodeHash;
        for (unsigned int i = 0; i < trace.depth; ++i) {
            boost::hash_combine(seed, nodeHash(trace.data[i]));
        }
        return seed;
    }
};

//TODO: merge per-thread output into single file
struct ThreadData
{
    ThreadData()
        : thread_id(next_thread_id++)
        , out(nullptr)
    {
        bool wasInHandler = in_handler;
        in_handler = true;
        ipCache.reserve(16384);
        traceCache.reserve(16384);
        string outputFileName = env("DUMP_MALLOC_TRACE_OUTPUT") + to_string(getpid()) + '.' + to_string(thread_id);
//         out = fopen(outputFileName.c_str(), "wa");
        out = stderr;
        if (!out) {
            fprintf(stderr, "Failed to open output file: %s\n", outputFileName.c_str());
            exit(1);
        }
        in_handler = wasInHandler;
    }

    ~ThreadData()
    {
        in_handler = true;
        fclose(out);
    }

    // assumes unique string ptrs as input, i.e. does not compare string data but only string ptrs
    unsigned int stringId(const char* string)
    {
        if (!strcmp(string, "")) {
            return 0;
        }

        auto it = stringCache.find(string);
        if (it != stringCache.end()) {
            return it->second;
        }

        auto id = next_string_id++;
        stringCache.insert(it, make_pair(string, id));
        fprintf(out, "s%u=%s\n", id, string);
        return id;
    }

    unordered_map<unw_word_t, IPCacheEntry> ipCache;
    unordered_map<Trace, unsigned int, TraceHasher> traceCache;
    // maps known file names and module names to string ID
    unordered_map<const char*, unsigned int> stringCache;
    unsigned int thread_id;
    FILE* out;
};

thread_local ThreadData threadData;

unsigned int print_caller(const int skip = 2)
{
    unw_context_t uc;
    unw_getcontext (&uc);

    unw_cursor_t cursor;
    unw_init_local (&cursor, &uc);

    // skip functions we are not interested in
    for (int i = 0; i < skip; ++i) {
        if (unw_step(&cursor) <= 0) {
            return 0;
        }
    }

    auto& ipCache = threadData.ipCache;

    Trace trace;
    trace.depth = 0;
    while (unw_step(&cursor) > 0 && trace.depth < Trace::MAX_DEPTH) {
        unw_word_t ip;
        unw_get_reg(&cursor, UNW_REG_IP, &ip);

        // check cache for known ip's
        auto it = ipCache.find(ip);
        if (it == ipCache.end()) {
            // not cached yet, get data
            btrace_info info;
            btrace_resolve_addr(&info, static_cast<uintptr_t>(ip),
                                ResolveFlags(DEMANGLE_FUNC | GET_FILENAME));

            const bool stop = !strcmp(info.function, "__libc_start_main")
                           || !strcmp(info.function, "__static_initialization_and_destruction_0");
            const unsigned int id = next_ipCache_id++;

            // and store it in the cache
            it = ipCache.insert(it, make_pair(ip, IPCacheEntry{id, stop}));

            const auto funcId = threadData.stringId(info.demangled_func_buf);
            const auto moduleId = threadData.stringId(info.module);
            const auto fileId = threadData.stringId(info.filename);

            fprintf(threadData.out, "%u=%u;%u;", id, funcId, moduleId);
            if (fileId) {
                if (info.linenumber > 0) {
                    fprintf(threadData.out, "%u:%d", fileId, info.linenumber);
                } else {
                    fprintf(threadData.out, "%u", fileId);
                }
            }
            fputs("\n", threadData.out);
        }

        const auto& frame = it->second;
        trace.data[trace.depth++] = frame.id;
        if (frame.stop) {
            break;
        }
    }

    if (trace.depth == 1) {
        return trace.data[0];
    }

    // TODO: sub-tree matching
    auto& traceCache = threadData.traceCache;
    auto it = traceCache.find(trace);
    if (it == traceCache.end()) {
        it = traceCache.insert(it, make_pair(trace, next_traceCache_id++));

        fprintf(threadData.out, "t%u=", it->second);
        for (unsigned int i = 0; i < trace.depth; ++i) {
            fprintf(threadData.out, "%u;", trace.data[i]);
        }
        fputs("\n", threadData.out);
    }

    return it->second;
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
        exit(1);
    }
    return buf + oldOffset;
}

void init()
{
    if (in_handler) {
        fprintf(stderr, "initialization recursion detected\n");
        exit(1);
    }

    in_handler = true;

    real_calloc = &dummy_calloc;
    real_calloc = findReal<calloc_t>("calloc");
    real_dlopen = findReal<dlopen_t>("dlopen");
    real_malloc = findReal<malloc_t>("malloc");
    real_free = findReal<free_t>("free");
    real_realloc = findReal<realloc_t>("realloc");
    real_posix_memalign = findReal<posix_memalign_t>("posix_memalign");
    real_valloc = findReal<valloc_t>("valloc");
    real_aligned_alloc = findReal<aligned_alloc_t>("aligned_alloc");

    btrace_dlopen_notify(nullptr);
    in_handler = false;
}

void handleMalloc(void* ptr, size_t size)
{
    const unsigned int treeId = print_caller();
    fprintf(threadData.out, "+%lu:%p %u\n", size, ptr, treeId);
}

void handleFree(void* ptr)
{
    fprintf(threadData.out, "-%p\n", ptr);
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

void* calloc(size_t num, size_t size)
{
    if (!real_calloc) {
        init();
    }

    void* ret = real_calloc(num, size);

    if (!in_handler) {
        in_handler = true;
        handleMalloc(ret, num*size);
        in_handler = false;
    }

    return ret;
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    if (!real_posix_memalign) {
        init();
    }

    int ret = real_posix_memalign(memptr, alignment, size);

    if (!in_handler) {
        in_handler = true;
        handleMalloc(*memptr, size);
        in_handler = false;
    }

    return ret;
}

void* aligned_alloc(size_t alignment, size_t size)
{
    if (!real_aligned_alloc) {
        init();
    }

    void* ret = real_aligned_alloc(alignment, size);

    if (!in_handler) {
        in_handler = true;
        handleMalloc(ret, size);
        in_handler = false;
    }

    return ret;
}

void* valloc(size_t size)
{
    if (!real_valloc) {
        init();
    }

    void* ret = real_valloc(size);

    if (!in_handler) {
        in_handler = true;
        handleMalloc(ret, size);
        in_handler = false;
    }

    return ret;
}

void *dlopen(const char *filename, int flag)
{
    if (!real_dlopen) {
        init();
    }

    void* ret = real_dlopen(filename, flag);

    if (!in_handler) {
        in_handler = true;
        btrace_dlopen_notify(filename);
        in_handler = false;
    }

    return ret;
}

}
