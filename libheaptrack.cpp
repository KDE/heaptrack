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

/**
 * Note: We use the C stdio API here for performance reasons.
 *       Esp. in multi-threaded environments this is much faster
 *       to produce non-per-line-interleaved output.
 */
atomic<FILE*> outputHandle(nullptr);

atomic<void(*)()> s_stopCallback(nullptr);

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
    outputHandle.store(nullptr);
    HandleGuard::inHandler = true;
}

static int dlopen_notify_callback(struct dl_phdr_info *info, size_t /*size*/, void *data)
{
    auto out = reinterpret_cast<FILE*>(data);
    const char *fileName = info->dlpi_name;
    if (!fileName || !fileName[0]) {
        fileName = "x";
    }

    if (fprintf(out, "m %s %lx", fileName, info->dlpi_addr) < 0) {
        heaptrack_stop();
        return 1;
    }
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const auto& phdr = info->dlpi_phdr[i];
        if (phdr.p_type == PT_LOAD) {
            if (fprintf(out, " %lx %lx", phdr.p_vaddr, phdr.p_memsz) < 0) {
                heaptrack_stop();
                return 1;
            }
        }
    }
    if (fputc('\n', out) == EOF) {
        heaptrack_stop();
        return 1;
    }

    return 0;
}

void updateModuleCache(FILE* out)
{
    if (fputs("m -\n", out) == EOF) {
        heaptrack_stop();
        return;
    }
    dl_iterate_phdr(dlopen_notify_callback, out);
    moduleCacheDirty = false;
}

struct Data
{
    void handleMalloc(void* ptr, size_t size, const Trace &trace, FILE* out)
    {
        if (lastTimerElapsed != timer.timesElapsed()) {
            lastTimerElapsed = timer.timesElapsed();
            if (fprintf(out, "c %lx\n", lastTimerElapsed) < 0) {
                heaptrack_stop();
                return;
            }
        }
        if (moduleCacheDirty) {
            updateModuleCache(out);
        }
        const size_t index = traceTree.index(trace, out);

#ifdef DEBUG_MALLOC_PTRS
        auto it = known.find(ptr);
        assert(it == known.end());
        known.insert(ptr);
#endif

        if (fprintf(out, "+ %lx %lx %lx\n", size, index, reinterpret_cast<uintptr_t>(ptr)) < 0) {
            heaptrack_stop();
            return;
        }
    }

    void handleFree(void* ptr, FILE* out)
    {
#ifdef DEBUG_MALLOC_PTRS
        auto it = known.find(ptr);
        assert(it != known.end());
        known.erase(it);
#endif

        if (fprintf(out, "- %lx\n", reinterpret_cast<uintptr_t>(ptr)) < 0) {
            heaptrack_stop();
            return;
        }
    }

    TraceTree traceTree;

    size_t lastTimerElapsed = 0;
    Timer timer;

#ifdef DEBUG_MALLOC_PTRS
    unordered_set<void*> known;
#endif

};

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

atomic<Data*> data;

}
extern "C" {

void heaptrack_init(const char *outputFileName_, void (*initCallbackBefore) (), void (*initCallbackAfter) (),
                    void (*stopCallback) ())
{
    HandleGuard guard;

    static mutex initMutex;
    lock_guard<mutex> lock(initMutex);

    if (data) {
        return;
    }

    if (initCallbackBefore) {
        initCallbackBefore();
    }

    pthread_atfork(&prepare_fork, &parent_fork, &child_fork);

    if (unw_set_caching_policy(unw_local_addr_space, UNW_CACHE_PER_THREAD)) {
        fprintf(stderr, "Failed to enable per-thread libunwind caching.\n");
    }
#if HAVE_UNW_SET_CACHE_LOG_SIZE
    if (unw_set_cache_log_size(unw_local_addr_space, 10)) {
        fprintf(stderr, "Failed to set libunwind cache size.\n");
    }
#endif

    string outputFileName;
    if (outputFileName_) {
        outputFileName.assign(outputFileName_);
    }

    FILE* out = nullptr;
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
    data.store(new Data);

    // ensure we properly cleanup on exit
    static once_flag once;
    call_once(once, [] {
        atexit([] () {
            // don't run the stop callback on global shutdown
            s_stopCallback.store(nullptr);
        });
        atexit(heaptrack_stop);
    });
    s_stopCallback.store(stopCallback);

    outputHandle.store(out);
    if (initCallbackAfter) {
        initCallbackAfter();
    }
}

void heaptrack_stop()
{
    HandleGuard guard;
    if (outputHandle) {
        flockfile(outputHandle.load());
        printf("shutting down heaptrack!\n");
        fclose(outputHandle.exchange(nullptr));
        delete data.exchange(nullptr);
        if (auto stop = s_stopCallback.exchange(nullptr)) {
            stop();
        }
    }
}

FILE* heaptrack_output_file()
{
    return outputHandle.load();
}

void heaptrack_malloc(void* ptr, size_t size)
{
    if (ptr && !HandleGuard::inHandler) {
        HandleGuard guard;

        Trace trace;
        if (!trace.fill(2)) {
            return;
        }

        if (FILE* out = outputHandle.load()) {
            LockGuard lock(out);
            if (Data* d = data.load()) {
                d->handleMalloc(ptr, size, trace, out);
            }
        }
    }
}

void heaptrack_free(void* ptr)
{
    if (ptr && !HandleGuard::inHandler) {
        HandleGuard guard;

        if (FILE* out = outputHandle.load()) {
            LockGuard lock(out);
            if (Data* d = data.load()) {
                d->handleFree(ptr, out);
            }
        }
    }
}

void heaptrack_realloc(void* ptr_in, size_t size, void* ptr_out)
{
    if (ptr_out && !HandleGuard::inHandler) {
        HandleGuard guard;

        Trace trace;
        if (!trace.fill(2)) {
            return;
        }

        if (FILE* out = outputHandle.load()) {
            LockGuard lock(out);

            if (Data* d = data.load()) {
                if (ptr_in) {
                    d->handleFree(ptr_in, out);
                }
                d->handleMalloc(ptr_out, size, trace, out);
            }
        }
    }
}

void heaptrack_invalidate_module_cache()
{
    moduleCacheDirty = true;
}

}
