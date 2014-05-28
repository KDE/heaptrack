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

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>
#include <tuple>

#include <boost/functional/hash.hpp>

#include <dlfcn.h>
#include <unistd.h>
#include <link.h>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

using namespace std;

using Trace = vector<unw_word_t>;

namespace std {
    template<>
    struct hash<Trace>
    {
        size_t operator() (const Trace& trace) const
        {
            size_t seed = 0;
            for (auto ip : trace) {
                boost::hash_combine(seed, ip);
            }
            boost::hash_combine(seed, trace.size());
            return seed;
        }
    };
}

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

thread_local Trace traceBuffer;

void trace(const int skip = 2)
{
    traceBuffer.clear();

    const size_t MAX_TRACE_SIZE = 64;
    traceBuffer.reserve(MAX_TRACE_SIZE);

    unw_context_t uc;
    unw_getcontext (&uc);

    unw_cursor_t cursor;
    unw_init_local (&cursor, &uc);

    // skip functions we are not interested in
    for (int i = 0; i < skip; ++i) {
        if (unw_step(&cursor) <= 0) {
            return;
        }
    }


    while (unw_step(&cursor) > 0 && traceBuffer.size() < MAX_TRACE_SIZE) {
        unw_word_t ip;
        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        traceBuffer.push_back(ip);
    }
}

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
    uintptr_t baseAddress;
    uint32_t size;
    unsigned int id;
    bool isExe;

    bool operator<(const Module& module) const
    {
        return make_tuple(baseAddress, size, fileName) < make_tuple(module.baseAddress, module.size, module.fileName);
    }
    bool operator!=(const Module& module) const
    {
        return make_tuple(baseAddress, size, fileName) != make_tuple(module.baseAddress, module.size, module.fileName);
    }
};

struct Data
{
    Data()
    {
        modules.reserve(32);
        ipCache.reserve(65536);
        traceCache.reserve(16384);
        allocationInfo.reserve(16384);

        string outputFileName = env("DUMP_MALLOC_TRACE_OUTPUT") + to_string(getpid());
        out = fopen(outputFileName.c_str(), "wa");
        if (!out) {
            fprintf(stderr, "Failed to open output file: %s\n", outputFileName.c_str());
            exit(1);
        }

        unsetenv("DUMP_MALLOC_TRACE_OUTPUT");
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
                ssize_t ret =  readlink("/proc/self/exe", buf, sizeof(buf));
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

        Module module{fileName, addressStart, static_cast<uint32_t>(addressEnd - addressStart), 0, isExe};

        auto it = lower_bound(modules.begin(), modules.end(), module);
        if (it == modules.end() || *it != module) {
            module.id = data->next_module_id++;
            fprintf(data->out, "m %u %s %lx %d\n", module.id, module.fileName.c_str(), module.baseAddress, module.isExe);
            modules.insert(it, module);
        }

        return 0;
    }

    void handleMalloc(void* ptr, size_t size)
    {
        trace();

        lock_guard<mutex> lock(m_mutex);
        if (moduleCacheDirty) {
            updateModuleCache();
        }
        auto it = traceCache.find(traceBuffer);
        if (it == traceCache.end()) {
            // cache before converting
            auto traceId = next_trace_id++;
            it = traceCache.insert(it, {traceBuffer, traceId});
            // ensure ip cache is up2date
            for (auto& ip : traceBuffer) {
                auto ipIt = ipCache.find(ip);
                if (ipIt == ipCache.end()) {
                    // find module and offset from cache
                    auto module = lower_bound(modules.begin(), modules.end(), ip,
                                                [] (const Module& module, const unw_word_t addr) -> bool {
                                                    return module.baseAddress + module.size < addr;
                                                });
                    if (module == modules.end()) {
                        ip = numeric_limits<unsigned int>::max();
                        continue;
                    }
                    auto ipId = next_ipCache_id++;
                    fprintf(out, "i %u %u %lx\n", ipId, module->id, ip - module->baseAddress);
                    ipIt = ipCache.insert(ipIt, {ip, ipId});
                }
                ip = ipIt->second;
            }
            // print trace
            fprintf(out, "t %u ", traceId);
            for (auto ipId : traceBuffer) {
                if (ipId == numeric_limits<unsigned int>::max()) {
                    continue;
                }
                fprintf(out, "%lu ", ipId);
            }
            fputc('\n', out);
        }
        allocationInfo[ptr] = {size, it->second};
        fprintf(out, "+ %lu %u\n", size, it->second);
    }

    void handleFree(void* ptr)
    {
        lock_guard<mutex> lock(m_mutex);
        auto it = allocationInfo.find(ptr);
        if (it == allocationInfo.end()) {
            return;
        }
        fprintf(out, "- %lu %u\n", it->second.size, it->second.traceId);
        allocationInfo.erase(it);
    }

    mutex m_mutex;
    unsigned int next_module_id = 0;
    unsigned int next_thread_id = 0;
    unsigned int next_ipCache_id = 0;
    unsigned int next_trace_id = 0;

    vector<Module> modules;
    unordered_map<unw_word_t, unw_word_t> ipCache;
    unordered_map<vector<unw_word_t>, unsigned int> traceCache;
    struct AllocationInfo
    {
        size_t size;
        unsigned int traceId;
    };
    unordered_map<void*, AllocationInfo> allocationInfo;
    FILE* out = nullptr;
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

    real_free(ptr);

    if (ptr && !HandleGuard::inHandler) {
        HandleGuard guard;
        data->handleFree(ptr);
    }
}

void* realloc(void* ptr, size_t size)
{
    if (!real_realloc) {
        init();
    }

    void* ret = real_realloc(ptr, size);

    if (ret && !HandleGuard::inHandler) {
        HandleGuard guard;
        data->handleFree(ptr);
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
