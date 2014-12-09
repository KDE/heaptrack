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

#include "libheaptrack.h"

#include <cstdio>
#include <cstdlib>
#include <stdio_ext.h>
#include <fcntl.h>
#include <link.h>

#include <atomic>
#include <string>
#include <memory>
#include <unordered_set>
#include <mutex>

#include <boost/algorithm/string/replace.hpp>

#include "tracetree.h"
#include "timer.h"

/**
 * uncomment this to get extended debug code for known pointers
 * there are still some malloc functions I'm missing apparently,
 * related to TLS and such I guess
 */
// #define DEBUG_MALLOC_PTRS

using namespace std;

namespace {

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

/**
 * Similar to std::lock_guard but operates on the internal stream lock of a FILE*.
 */
class LockGuard
{
public:
    LockGuard(FILE* file)
        : file(file)
    {
        flockfile(file);
    }

    ~LockGuard()
    {
        funlockfile(file);
    }

private:
    FILE* file;
};

thread_local bool HandleGuard::inHandler = false;

void writeExe(FILE* out)
{
    const int BUF_SIZE = 1023;
    char buf[BUF_SIZE + 1];
    ssize_t size = readlink("/proc/self/exe", buf, BUF_SIZE);
    if (size > 0 && size < BUF_SIZE) {
        buf[size] = 0;
        fprintf(out, "x %s\n", buf);
    }
}

void writeCommandLine(FILE* out)
{
    fputc('X', out);
    const int BUF_SIZE = 4096;
    char buf[BUF_SIZE + 1];
    auto fd = open("/proc/self/cmdline", O_RDONLY);
    int bytesRead = read(fd, buf, BUF_SIZE);
    char *end = buf + bytesRead;
    for (char *p = buf; p < end;) {
        fputc(' ', out);
        fputs(p, out);
        while (*p++); // skip until start of next 0-terminated section
    }

    close(fd);
    fputc('\n', out);
}

void prepare_fork();
void parent_fork();
void child_fork();

struct Data
{
    Data(const char *outputFileName_)
    {
        pthread_atfork(&prepare_fork, &parent_fork, &child_fork);

        string outputFileName;
        if (outputFileName_) {
            outputFileName.assign(outputFileName_);
        }
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
            __fsetlocking(out, FSETLOCKING_BYCALLER);
        }

        if (!out) {
            fprintf(stderr, "Failed to open output file: %s\n", outputFileName.c_str());
            exit(1);
        }

        writeExe(out);
        writeCommandLine(out);

        // TODO: remember meta data about host application, such as cmdline, date of run, ...

        // cleanup environment to prevent tracing of child apps
        unsetenv("DUMP_HEAPTRACK_OUTPUT");

        // print a backtrace in every interval
        timer.setInterval(0, 1000 * 1000 * 10);
    }

    ~Data()
    {
        HandleGuard::inHandler = true;
        if (out) {
            fclose(out);
        }
    }

    void updateModuleCache()
    {
        fputs("m -\n", out);
        dl_iterate_phdr(dlopen_notify_callback, this);
        moduleCacheDirty = false;
    }

    static int dlopen_notify_callback(struct dl_phdr_info *info, size_t /*size*/, void *_data)
    {
        auto data = reinterpret_cast<Data*>(_data);
        const char *fileName = info->dlpi_name;
        if (!fileName || !fileName[0]) {
            fileName = "x";
        }

        fprintf(data->out, "m %s %lx", fileName, info->dlpi_addr);
        for (int i = 0; i < info->dlpi_phnum; i++) {
            const auto& phdr = info->dlpi_phdr[i];
            if (phdr.p_type == PT_LOAD) {
                fprintf(data->out, " %lx %lx", phdr.p_vaddr, phdr.p_memsz);
            }
        }
        fputc('\n', data->out);

        return 0;
    }

    void handleMalloc(void* ptr, size_t size)
    {
        Trace trace;
        if (!trace.fill(3)) {
            return;
        }

        LockGuard lock(out);
        if (lastTimerElapsed != timer.timesElapsed()) {
            lastTimerElapsed = timer.timesElapsed();
            fprintf(out, "c %lx\n", lastTimerElapsed);
        }
        if (moduleCacheDirty) {
            updateModuleCache();
        }
        const size_t index = traceTree.index(trace, out);

#ifdef DEBUG_MALLOC_PTRS
        auto it = known.find(ptr);
        assert(it == known.end());
        known.insert(ptr);
#endif

        fprintf(out, "+ %lx %lx %lx\n", size, index, reinterpret_cast<uintptr_t>(ptr));
    }

    void handleFree(void* ptr)
    {
        LockGuard lock(out);

#ifdef DEBUG_MALLOC_PTRS
        auto it = known.find(ptr);
        assert(it != known.end());
        known.erase(it);
#endif

        fprintf(out, "- %lx\n", reinterpret_cast<uintptr_t>(ptr));
    }

    TraceTree traceTree;
    /**
     * Note: We use the C stdio API here for performance reasons.
     *       Esp. in multi-threaded environments this is much faster
     *       to produce non-per-line-interleaved output.
     */
    FILE* out = nullptr;

    size_t lastTimerElapsed = 0;
    Timer timer;

#ifdef DEBUG_MALLOC_PTRS
    unordered_set<void*> known;
#endif

};

unique_ptr<Data> data;

void prepare_fork()
{
    // don't do any custom malloc handling while inside fork
    HandleGuard::inHandler = true;
}

void parent_fork()
{
    // the parent process can now continue its custom malloc tracking
    HandleGuard::inHandler = false;
}

void child_fork()
{
    // but the forked child process cleans up itself
    // this is important to prevent two processes writing to the same file
    if (data) {
        data->out = nullptr;
        data.reset(nullptr);
    }
    HandleGuard::inHandler = true;
}

}
extern "C" {

void heaptrack_init(const char *outputFileName, void (*initCallbackBefore) (), void (*initCallbackAfter) ())
{
    HandleGuard guard;

    static once_flag once;
    call_once(once, [=] {
        if (data) {
            fprintf(stderr, "initialization recursion detected\n");
            abort();
        }

        if (initCallbackBefore) {
            initCallbackBefore();
        }

        if (unw_set_caching_policy(unw_local_addr_space, UNW_CACHE_PER_THREAD)) {
            fprintf(stderr, "Failed to enable per-thread libunwind caching.\n");
        }
        if (unw_set_cache_log_size(unw_local_addr_space, 10)) {
            fprintf(stderr, "Failed to set libunwind cache size.\n");
        }

        data.reset(new Data(outputFileName));

        if (initCallbackAfter) {
            initCallbackAfter();
        }
    });
}

FILE* heaptrack_output_file()
{
    return data ? data->out : nullptr;
}

void heaptrack_malloc(void* ptr, size_t size)
{
    if (ptr && !HandleGuard::inHandler && data) {
        HandleGuard guard;
        data->handleMalloc(ptr, size);
    }
}

void heaptrack_free(void* ptr)
{
    if (ptr && !HandleGuard::inHandler && data) {
        HandleGuard guard;
        data->handleFree(ptr);
    }
}

void heaptrack_realloc(void* ptr_in, size_t size, void* ptr_out)
{
    if (ptr_out && !HandleGuard::inHandler && data) {
        HandleGuard guard;
        if (ptr_in) {
            data->handleFree(ptr_in);
        }
        data->handleMalloc(ptr_out, size);
    }
}

void heaptrack_invalidate_module_cache()
{
    moduleCacheDirty = true;
}

}
