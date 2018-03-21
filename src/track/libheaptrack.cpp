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

/**
 * @file libheaptrack.cpp
 *
 * @brief Collect raw heaptrack data by overloading heap allocation functions.
 */

#include "libheaptrack.h"

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <link.h>
#include <stdio_ext.h>
#include <pthread.h>
#include <signal.h>

#include <atomic>
#include <cinttypes>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

#include <boost/algorithm/string/replace.hpp>

#include "tracetree.h"
#include "util/config.h"
#include "util/libunwind_config.h"

/**
 * uncomment this to get extended debug code for known pointers
 * there are still some malloc functions I'm missing apparently,
 * related to TLS and such I guess
 */
// #define DEBUG_MALLOC_PTRS

using namespace std;

namespace {

enum DebugVerbosity
{
    NoDebugOutput,
    MinimalOutput,
    VerboseOutput,
    VeryVerboseOutput,
};

// change this to add more debug output to stderr
constexpr const DebugVerbosity s_debugVerbosity = NoDebugOutput;

/**
 * Call this to optionally show debug information but give the compiler
 * a hand in removing it all if debug output is disabled.
 */
template <DebugVerbosity debugLevel, typename... Args>
inline void debugLog(const char fmt[], Args... args)
{
    if (debugLevel <= s_debugVerbosity) {
        flockfile(stderr);
        fprintf(stderr, "heaptrack debug [%d]: ", static_cast<int>(debugLevel));
        fprintf(stderr, fmt, args...);
        fputc('\n', stderr);
        funlockfile(stderr);
    }
}

/**
 * Set to true in an atexit handler. In such conditions, the stop callback
 * will not be called.
 */
atomic<bool> s_atexit{false};

/**
 * Set to true in heaptrack_stop, when s_atexit was not yet set. In such conditions,
 * we always fully unload and cleanup behind ourselves
 */
atomic<bool> s_forceCleanup{false};

/**
 * A per-thread handle guard to prevent infinite recursion, which should be
 * acquired before doing any special symbol handling.
 */
struct RecursionGuard
{
    RecursionGuard()
        : wasLocked(isActive)
    {
        isActive = true;
    }

    ~RecursionGuard()
    {
        isActive = wasLocked;
    }

    const bool wasLocked;
    static thread_local bool isActive;
};

thread_local bool RecursionGuard::isActive = false;

void writeVersion(FILE* out)
{
    fprintf(out, "v %x %x\n", HEAPTRACK_VERSION, HEAPTRACK_FILE_FORMAT_VERSION);
}

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
    char* end = buf + bytesRead;
    for (char* p = buf; p < end;) {
        fputc(' ', out);
        fputs(p, out);
        while (*p++)
            ; // skip until start of next 0-terminated section
    }

    close(fd);
    fputc('\n', out);
}

void writeSystemInfo(FILE* out)
{
    fprintf(out, "I %lx %lx\n", sysconf(_SC_PAGESIZE), sysconf(_SC_PHYS_PAGES));
}

FILE* createFile(const char* fileName)
{
    string outputFileName;
    if (fileName) {
        outputFileName.assign(fileName);
    }

    if (outputFileName == "-" || outputFileName == "stdout") {
        debugLog<VerboseOutput>("%s", "will write to stdout");
        return stdout;
    } else if (outputFileName == "stderr") {
        debugLog<VerboseOutput>("%s", "will write to stderr");
        return stderr;
    }

    if (outputFileName.empty()) {
        // env var might not be set when linked directly into an executable
        outputFileName = "heaptrack.$$";
    }

    boost::replace_all(outputFileName, "$$", to_string(getpid()));

    auto out = fopen(outputFileName.c_str(), "w");
    debugLog<VerboseOutput>("will write to %s/%p\n", outputFileName.c_str(), out);
    // we do our own locking, this speeds up the writing significantly
    if (out) {
        __fsetlocking(out, FSETLOCKING_BYCALLER);
    } else {
        fprintf(stderr, "ERROR: failed to open heaptrack output file %s: %s (%d)\n",
                outputFileName.c_str(), strerror(errno), errno);
    }

    return out;
}

/**
 * Thread-Safe heaptrack API
 *
 * The only critical section in libheaptrack is the output of the data,
 * dl_iterate_phdr
 * calls, as well as initialization and shutdown.
 *
 * This uses a spinlock, instead of a std::mutex, as the latter can lead to
 * deadlocks
 * on destruction. The spinlock is "simple", and OK to only guard the small
 * sections.
 */
class HeapTrack
{
public:
    HeapTrack(const RecursionGuard& /*recursionGuard*/)
        : HeapTrack([] { return true; })
    {
    }

    ~HeapTrack()
    {
        debugLog<VeryVerboseOutput>("%s", "releasing lock");
        s_locked.store(false, memory_order_release);
    }

    void initialize(const char* fileName, heaptrack_callback_t initBeforeCallback,
                    heaptrack_callback_initialized_t initAfterCallback, heaptrack_callback_t stopCallback)
    {
        debugLog<MinimalOutput>("initializing: %s", fileName);
        if (s_data) {
            debugLog<MinimalOutput>("%s", "already initialized");
            return;
        }

        if (initBeforeCallback) {
            debugLog<MinimalOutput>("%s", "calling initBeforeCallback");
            initBeforeCallback();
            debugLog<MinimalOutput>("%s", "done calling initBeforeCallback");
        }

        // do some once-only initializations
        static once_flag once;
        call_once(once, [] {
            debugLog<MinimalOutput>("%s", "doing once-only initialization");
            // configure libunwind for better speed
            if (unw_set_caching_policy(unw_local_addr_space, UNW_CACHE_PER_THREAD)) {
                fprintf(stderr, "WARNING: Failed to enable per-thread libunwind caching.\n");
            }
#ifdef unw_set_cache_size
            if (unw_set_cache_size(unw_local_addr_space, 1024, 0)) {
                fprintf(stderr, "WARNING: Failed to set libunwind cache size.\n");
            }
#endif

            // do not trace forked child processes
            // TODO: make this configurable
            pthread_atfork(&prepare_fork, &parent_fork, &child_fork);

            atexit([]() {
                if (s_forceCleanup) {
                    return;
                }
                debugLog<MinimalOutput>("%s", "atexit()");
                s_atexit.store(true);
                heaptrack_stop();
            });
        });

        FILE* out = createFile(fileName);

        if (!out) {
            if (stopCallback) {
                stopCallback();
            }
            return;
        }

        writeVersion(out);
        writeExe(out);
        writeCommandLine(out);
        writeSystemInfo(out);

        s_data = new LockedData(out, stopCallback);

        if (initAfterCallback) {
            debugLog<MinimalOutput>("%s", "calling initAfterCallback");
            initAfterCallback(out);
            debugLog<MinimalOutput>("%s", "calling initAfterCallback done");
        }

        debugLog<MinimalOutput>("%s", "initialization done");
    }

    void shutdown()
    {
        if (!s_data) {
            return;
        }

        debugLog<MinimalOutput>("%s", "shutdown()");

        writeTimestamp();
        writeRSS();

        // NOTE: we leak heaptrack data on exit, intentionally
        // This way, we can be sure to get all static deallocations.
        if (!s_atexit || s_forceCleanup) {
            delete s_data;
            s_data = nullptr;
        }

        debugLog<MinimalOutput>("%s", "shutdown() done");
    }

    void invalidateModuleCache()
    {
        if (!s_data) {
            return;
        }
        s_data->moduleCacheDirty = true;
    }

    void writeTimestamp()
    {
        if (!s_data || !s_data->out) {
            return;
        }

        auto elapsed = chrono::duration_cast<chrono::milliseconds>(clock::now() - s_data->start);

        debugLog<VeryVerboseOutput>("writeTimestamp(%" PRIx64 ")", elapsed.count());

        if (fprintf(s_data->out, "c %" PRIx64 "\n", elapsed.count()) < 0) {
            writeError();
            return;
        }
    }

    void writeRSS()
    {
        if (!s_data || !s_data->out || !s_data->procStatm) {
            return;
        }

        // read RSS in pages from statm, then rewind for next read
        size_t rss = 0;
        if (fscanf(s_data->procStatm, "%*x %zx", &rss) != 1) {
            fprintf(stderr, "WARNING: Failed to read RSS value from /proc/self/statm.\n");
            fclose(s_data->procStatm);
            s_data->procStatm = nullptr;
            return;
        }
        rewind(s_data->procStatm);
        // TODO: compare to rusage.ru_maxrss (getrusage) to find "real" peak?
        // TODO: use custom allocators with known page sizes to prevent tainting
        //       the RSS numbers with heaptrack-internal data

        if (fprintf(s_data->out, "R %zx\n", rss) < 0) {
            writeError();
            return;
        }
    }

    void handleMalloc(void* ptr, size_t size, const Trace& trace)
    {
        if (!s_data || !s_data->out) {
            return;
        }
        updateModuleCache();
        const auto index = s_data->traceTree.index(trace, s_data->out);

#ifdef DEBUG_MALLOC_PTRS
        auto it = s_data->known.find(ptr);
        assert(it == s_data->known.end());
        s_data->known.insert(ptr);
#endif

        if (fprintf(s_data->out, "+ %zx %x %" PRIxPTR "\n", size, index, reinterpret_cast<uintptr_t>(ptr)) < 0) {
            writeError();
            return;
        }
    }

    void handleFree(void* ptr)
    {
        if (!s_data || !s_data->out) {
            return;
        }

#ifdef DEBUG_MALLOC_PTRS
        auto it = s_data->known.find(ptr);
        assert(it != s_data->known.end());
        s_data->known.erase(it);
#endif

        if (fprintf(s_data->out, "- %" PRIxPTR "\n", reinterpret_cast<uintptr_t>(ptr)) < 0) {
            writeError();
            return;
        }
    }

private:
    static int dl_iterate_phdr_callback(struct dl_phdr_info* info, size_t /*size*/, void* data)
    {
        auto heaptrack = reinterpret_cast<HeapTrack*>(data);
        const char* fileName = info->dlpi_name;
        if (!fileName || !fileName[0]) {
            fileName = "x";
        }

        debugLog<VerboseOutput>("dlopen_notify_callback: %s %zx", fileName, info->dlpi_addr);

        if (fprintf(heaptrack->s_data->out, "m %s %zx", fileName, info->dlpi_addr) < 0) {
            heaptrack->writeError();
            return 1;
        }

        for (int i = 0; i < info->dlpi_phnum; i++) {
            const auto& phdr = info->dlpi_phdr[i];
            if (phdr.p_type == PT_LOAD) {
                if (fprintf(heaptrack->s_data->out, " %zx %zx", phdr.p_vaddr, phdr.p_memsz) < 0) {
                    heaptrack->writeError();
                    return 1;
                }
            }
        }

        if (fputc('\n', heaptrack->s_data->out) == EOF) {
            heaptrack->writeError();
            return 1;
        }

        return 0;
    }

    static void prepare_fork()
    {
        debugLog<MinimalOutput>("%s", "prepare_fork()");
        // don't do any custom malloc handling while inside fork
        RecursionGuard::isActive = true;
    }

    static void parent_fork()
    {
        debugLog<MinimalOutput>("%s", "parent_fork()");
        // the parent process can now continue its custom malloc tracking
        RecursionGuard::isActive = false;
    }

    static void child_fork()
    {
        debugLog<MinimalOutput>("%s", "child_fork()");
        // but the forked child process cleans up itself
        // this is important to prevent two processes writing to the same file
        s_data = nullptr;
        RecursionGuard::isActive = true;
    }

    void updateModuleCache()
    {
        if (!s_data || !s_data->out || !s_data->moduleCacheDirty) {
            return;
        }
        debugLog<MinimalOutput>("%s", "updateModuleCache()");
        if (fputs("m -\n", s_data->out) == EOF) {
            writeError();
            return;
        }
        dl_iterate_phdr(&dl_iterate_phdr_callback, this);
        s_data->moduleCacheDirty = false;
    }

    void writeError()
    {
        debugLog<MinimalOutput>("write error %d/%s", errno, strerror(errno));
        s_data->out = nullptr;
        shutdown();
    }

    template <typename AdditionalLockCheck>
    HeapTrack(AdditionalLockCheck lockCheck)
    {
        debugLog<VeryVerboseOutput>("%s", "acquiring lock");
        while (s_locked.exchange(true, memory_order_acquire) && lockCheck()) {
            this_thread::sleep_for(chrono::microseconds(1));
        }
        debugLog<VeryVerboseOutput>("%s", "lock acquired");
    }

    using clock = chrono::steady_clock;

    struct LockedData
    {
        LockedData(FILE* out, heaptrack_callback_t stopCallback)
            : out(out)
            , stopCallback(stopCallback)
        {
            debugLog<MinimalOutput>("%s", "constructing LockedData");
            procStatm = fopen("/proc/self/statm", "r");
            if (!procStatm) {
                fprintf(stderr, "WARNING: Failed to open /proc/self/statm for reading: %s.\n", strerror(errno));
            } else if (setvbuf(procStatm, nullptr, _IONBF, 0)) {
                // disable buffering to ensure we read the latest values
                fprintf(stderr, "WARNING: Failed to disable buffering for reading of /proc/self/statm: %s.\n", strerror(errno));
            }

            // ensure this utility thread is not handling any signals
            // our host application may assume only one specific thread
            // will handle the threads, if that's not the case things
            // seemingly break in non-obvious ways.
            // see also: https://bugs.kde.org/show_bug.cgi?id=378494
            sigset_t previousMask;
            sigset_t newMask;
            sigfillset(&newMask);
            if (pthread_sigmask(SIG_SETMASK, &newMask, &previousMask) != 0) {
                fprintf(stderr, "WARNING: Failed to block signals, disabling timer thread.\n");
                return;
            }

            // the mask we set above will be inherited by the thread that we spawn below
            timerThread = thread([&]() {
                RecursionGuard::isActive = true;
                debugLog<MinimalOutput>("%s", "timer thread started");

                // now loop and repeatedly print the timestamp and RSS usage to the data stream
                while (!stopTimerThread) {
                    // TODO: make interval customizable
                    this_thread::sleep_for(chrono::milliseconds(10));

                    HeapTrack heaptrack([&] { return !stopTimerThread.load(); });
                    if (!stopTimerThread) {
                        heaptrack.writeTimestamp();
                        heaptrack.writeRSS();
                    }
                }
            });

            // now restore the previous mask as if nothing ever happened
            if (pthread_sigmask(SIG_SETMASK, &previousMask, nullptr) != 0) {
                fprintf(stderr, "WARNING: Failed to restore the signal mask.\n");
            }
        }

        ~LockedData()
        {
            debugLog<MinimalOutput>("%s", "destroying LockedData");
            stopTimerThread = true;
            if (timerThread.joinable()) {
                try {
                    timerThread.join();
                } catch (std::system_error) {
                }
            }

            if (out) {
                fclose(out);
            }

            if (procStatm) {
                fclose(procStatm);
            }

            if (stopCallback && (!s_atexit || s_forceCleanup)) {
                stopCallback();
            }
            debugLog<MinimalOutput>("%s", "done destroying LockedData");
        }

        /**
         * Note: We use the C stdio API here for performance reasons.
         *       Esp. in multi-threaded environments this is much faster
         *       to produce non-per-line-interleaved output.
         */
        FILE* out = nullptr;

        /// /proc/self/statm file stream to read RSS value from
        FILE* procStatm = nullptr;

        /**
         * Calls to dlopen/dlclose mark the cache as dirty.
         * When this happened, all modules and their section addresses
         * must be found again via dl_iterate_phdr before we output the
         * next instruction pointer. Otherwise, heaptrack_interpret might
         * encounter IPs of an unknown/invalid module.
         */
        bool moduleCacheDirty = true;

        TraceTree traceTree;

        const chrono::time_point<clock> start = clock::now();
        atomic<bool> stopTimerThread{false};
        thread timerThread;

        heaptrack_callback_t stopCallback = nullptr;

#ifdef DEBUG_MALLOC_PTRS
        unordered_set<void*> known;
#endif
    };

    static atomic<bool> s_locked;
    static LockedData* s_data;
};

atomic<bool> HeapTrack::s_locked{false};
HeapTrack::LockedData* HeapTrack::s_data{nullptr};
}
extern "C" {

void heaptrack_init(const char* outputFileName, heaptrack_callback_t initBeforeCallback,
                    heaptrack_callback_initialized_t initAfterCallback, heaptrack_callback_t stopCallback)
{
    RecursionGuard guard;

    debugLog<MinimalOutput>("heaptrack_init(%s)", outputFileName);

    HeapTrack heaptrack(guard);
    heaptrack.initialize(outputFileName, initBeforeCallback, initAfterCallback, stopCallback);
}

void heaptrack_stop()
{
    RecursionGuard guard;

    debugLog<MinimalOutput>("%s", "heaptrack_stop()");

    HeapTrack heaptrack(guard);

    if (!s_atexit) {
        s_forceCleanup.store(true);
    }

    heaptrack.shutdown();
}

void heaptrack_malloc(void* ptr, size_t size)
{
    if (ptr && !RecursionGuard::isActive) {
        RecursionGuard guard;

        debugLog<VeryVerboseOutput>("heaptrack_malloc(%p, %zu)", ptr, size);

        Trace trace;
        trace.fill(2 + HEAPTRACK_DEBUG_BUILD);

        HeapTrack heaptrack(guard);
        heaptrack.handleMalloc(ptr, size, trace);
    }
}

void heaptrack_free(void* ptr)
{
    if (ptr && !RecursionGuard::isActive) {
        RecursionGuard guard;

        debugLog<VeryVerboseOutput>("heaptrack_free(%p)", ptr);

        HeapTrack heaptrack(guard);
        heaptrack.handleFree(ptr);
    }
}

void heaptrack_realloc(void* ptr_in, size_t size, void* ptr_out)
{
    if (ptr_out && !RecursionGuard::isActive) {
        RecursionGuard guard;

        debugLog<VeryVerboseOutput>("heaptrack_realloc(%p, %zu, %p)", ptr_in, size, ptr_out);

        Trace trace;
        trace.fill(2 + HEAPTRACK_DEBUG_BUILD);

        HeapTrack heaptrack(guard);
        if (ptr_in) {
            heaptrack.handleFree(ptr_in);
        }
        heaptrack.handleMalloc(ptr_out, size, trace);
    }
}

void heaptrack_invalidate_module_cache()
{
    RecursionGuard guard;

    debugLog<VerboseOutput>("%s", "heaptrack_invalidate_module_cache()");

    HeapTrack heaptrack(guard);
    heaptrack.invalidateModuleCache();
}
}
