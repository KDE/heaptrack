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
#include <pthread.h>
#include <signal.h>
#include <stdio_ext.h>
#include <sys/file.h>
#include <syscall.h>

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
#include "util/linewriter.h"

extern "C" {
__attribute__((weak)) void __libc_freeres();
}
namespace __gnu_cxx {
__attribute__((weak)) extern void __freeres();
}

/**
 * uncomment this to get extended debug code for known pointers
 * there are still some malloc functions I'm missing apparently,
 * related to TLS and such I guess
 */
// #define DEBUG_MALLOC_PTRS

using namespace std;

namespace {

using clock = chrono::steady_clock;
chrono::time_point<clock> startTime()
{
    static const chrono::time_point<clock> s_start = clock::now();
    return s_start;
}

chrono::milliseconds elapsedTime()
{
    return chrono::duration_cast<chrono::milliseconds>(clock::now() - startTime());
}

__pid_t gettid()
{
    return syscall(SYS_gettid);
}

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
        RecursionGuard guard;
        flockfile(stderr);
        fprintf(stderr, "heaptrack debug(%d) [%d:%d]@%" PRIu64 " ", static_cast<int>(debugLevel), getpid(), gettid(),
                elapsedTime().count());
        fprintf(stderr, fmt, args...);
        fputc('\n', stderr);
        funlockfile(stderr);
    }
}

void printBacktrace()
{
    if (s_debugVerbosity == NoDebugOutput)
        return;

    RecursionGuard guard;

    Trace::print();
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

int createFile(const char* fileName)
{
    string outputFileName;
    if (fileName) {
        outputFileName.assign(fileName);
    }

    if (outputFileName == "-" || outputFileName == "stdout") {
        debugLog<VerboseOutput>("%s", "will write to stdout");
        return fileno(stdout);
    } else if (outputFileName == "stderr") {
        debugLog<VerboseOutput>("%s", "will write to stderr");
        return fileno(stderr);
    }

    if (outputFileName.empty()) {
        // env var might not be set when linked directly into an executable
        outputFileName = "heaptrack.$$";
    }

    boost::replace_all(outputFileName, "$$", to_string(getpid()));

    auto out = open(outputFileName.c_str(), O_CREAT | O_WRONLY | O_CLOEXEC, 0644);
    debugLog<VerboseOutput>("will write to %s/%p\n", outputFileName.c_str(), out);
    // we do our own locking, this speeds up the writing significantly
    if (out == -1) {
        fprintf(stderr, "ERROR: failed to open heaptrack output file %s: %s (%d)\n", outputFileName.c_str(),
                strerror(errno), errno);
    } else if (flock(out, LOCK_EX | LOCK_NB) != 0) {
        fprintf(stderr, "ERROR: failed to lock heaptrack output file %s: %s (%d)\n", outputFileName.c_str(),
                strerror(errno), errno);
        close(out);
        return -1;
    }

    return out;
}

/**
 * Thread-Safe heaptrack API
 *
 * The only critical section in libheaptrack is the output of the data,
 * dl_iterate_phdr
 * calls, as well as initialization and shutdown.
 */
class HeapTrack
{
public:
    HeapTrack(const RecursionGuard& /*recursionGuard*/)
    {
        debugLog<VeryVerboseOutput>("%s", "acquiring lock");
        s_lock.lock();
        debugLog<VeryVerboseOutput>("%s", "lock acquired");
    }

    ~HeapTrack()
    {
        debugLog<VeryVerboseOutput>("%s", "releasing lock");
        s_lock.unlock();
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

            Trace::setup();

            // do not trace forked child processes
            // TODO: make this configurable
            pthread_atfork(&prepare_fork, &parent_fork, &child_fork);

            atexit([]() {
                if (s_forceCleanup) {
                    return;
                }
                debugLog<MinimalOutput>("%s", "atexit()");

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

                s_atexit.store(true);
                heaptrack_stop();
            });
        });

        const auto out = createFile(fileName);

        if (out == -1) {
            if (stopCallback) {
                stopCallback();
            }
            return;
        }

        s_data = new LockedData(out, stopCallback);

        writeVersion();
        writeExe();
        writeCommandLine();
        writeSystemInfo();

        if (initAfterCallback) {
            debugLog<MinimalOutput>("%s", "calling initAfterCallback");
            initAfterCallback(s_data->out);
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

        s_data->out.flush();
        s_data->out.close();

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
        if (!s_data || !s_data->out.canWrite()) {
            return;
        }

        auto elapsed = elapsedTime();

        debugLog<VeryVerboseOutput>("writeTimestamp(%" PRIx64 ")", elapsed.count());

        s_data->out.writeHexLine('c', static_cast<size_t>(elapsed.count()));
    }

    void writeRSS()
    {
        if (!s_data || !s_data->out.canWrite() || s_data->procStatm == -1) {
            return;
        }

        // read RSS in pages from statm, then rewind for next read
        // NOTE: don't use fscanf here, it could potentially deadlock us
        const int BUF_SIZE = 512;
        char buf[BUF_SIZE + 1];
        if (read(s_data->procStatm, buf, BUF_SIZE) <= 0) {
            fprintf(stderr, "WARNING: Failed to read RSS value from /proc/self/statm.\n");
            close(s_data->procStatm);
            s_data->procStatm = -1;
            return;
        }
        lseek(s_data->procStatm, 0, SEEK_SET);

        size_t rss = 0;
        if (sscanf(buf, "%*u %zu", &rss) != 1) {
            fprintf(stderr, "WARNING: Failed to read RSS value from /proc/self/statm.\n");
            close(s_data->procStatm);
            s_data->procStatm = -1;
            return;
        }

        // TODO: compare to rusage.ru_maxrss (getrusage) to find "real" peak?
        // TODO: use custom allocators with known page sizes to prevent tainting
        //       the RSS numbers with heaptrack-internal data

        s_data->out.writeHexLine('R', rss);
    }

    void writeVersion()
    {
        s_data->out.writeHexLine('v', static_cast<size_t>(HEAPTRACK_VERSION),
                                 static_cast<size_t>(HEAPTRACK_FILE_FORMAT_VERSION));
    }

    void writeExe()
    {
        const int BUF_SIZE = 1023;
        char buf[BUF_SIZE + 1];
        ssize_t size = readlink("/proc/self/exe", buf, BUF_SIZE);
        if (size > 0 && size < BUF_SIZE) {
            buf[size] = 0;
            s_data->out.write("x %s\n", buf);
        }
    }

    void writeCommandLine()
    {
        s_data->out.write("X");
        const int BUF_SIZE = 4096;
        char buf[BUF_SIZE + 1];
        auto fd = open("/proc/self/cmdline", O_RDONLY);
        int bytesRead = read(fd, buf, BUF_SIZE);
        char* end = buf + bytesRead;
        for (char* p = buf; p < end;) {
            s_data->out.write(" %s", p);
            while (*p++)
                ; // skip until start of next 0-terminated section
        }

        close(fd);
        s_data->out.write("\n");
    }

    void writeSystemInfo()
    {
        s_data->out.writeHexLine('I', static_cast<size_t>(sysconf(_SC_PAGESIZE)),
                                 static_cast<size_t>(sysconf(_SC_PHYS_PAGES)));
    }

    void handleMalloc(void* ptr, size_t size, const Trace& trace)
    {
        if (!s_data || !s_data->out.canWrite()) {
            return;
        }
        updateModuleCache();

        const auto index = s_data->traceTree.index(
            trace, [](uintptr_t ip, uint32_t index) { return s_data->out.writeHexLine('t', ip, index); });

#ifdef DEBUG_MALLOC_PTRS
        auto it = s_data->known.find(ptr);
        assert(it == s_data->known.end());
        s_data->known.insert(ptr);
#endif

        s_data->out.writeHexLine('+', size, index, reinterpret_cast<uintptr_t>(ptr));
    }

    void handleFree(void* ptr)
    {
        if (!s_data || !s_data->out.canWrite()) {
            return;
        }

#ifdef DEBUG_MALLOC_PTRS
        auto it = s_data->known.find(ptr);
        assert(it != s_data->known.end());
        s_data->known.erase(it);
#endif

        s_data->out.writeHexLine('-', reinterpret_cast<uintptr_t>(ptr));
    }

    static bool isPaused()
    {
        return s_paused;
    }

    static void setPaused(bool state)
    {
        s_paused = state;
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

        if (!heaptrack->s_data->out.write("m %s %zx", fileName, info->dlpi_addr)) {
            return 1;
        }

        for (int i = 0; i < info->dlpi_phnum; i++) {
            const auto& phdr = info->dlpi_phdr[i];
            if (phdr.p_type == PT_LOAD) {
                if (!heaptrack->s_data->out.write(" %zx %zx", phdr.p_vaddr, phdr.p_memsz)) {
                    return 1;
                }
            }
        }

        if (!heaptrack->s_data->out.write("\n")) {
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
        if (!s_data || !s_data->out.canWrite() || !s_data->moduleCacheDirty) {
            return;
        }
        debugLog<MinimalOutput>("%s", "updateModuleCache()");
        if (!s_data->out.write("m -\n")) {
            return;
        }
        dl_iterate_phdr(&dl_iterate_phdr_callback, this);
        s_data->moduleCacheDirty = false;
    }

    void writeError()
    {
        debugLog<MinimalOutput>("write error %d/%s", errno, strerror(errno));
        printBacktrace();
        shutdown();
    }

    struct LockCheckFailed{};

    /**
     * To prevent deadlocks on shutdown, we try to lock from the timer thread
     * and throw an LockCheckFailed exception otherwise.
     */
    template <typename AdditionalLockCheck>
    HeapTrack(AdditionalLockCheck lockCheck)
    {
        debugLog<VeryVerboseOutput>("%s", "trying to acquire lock");
        while (!s_lock.try_lock()) {
            if (!lockCheck())
                throw LockCheckFailed();
            this_thread::sleep_for(chrono::microseconds(1));
        }
        debugLog<VeryVerboseOutput>("%s", "lock acquired");
    }

    struct LockedData
    {
        LockedData(int out, heaptrack_callback_t stopCallback)
            : out(out)
            , stopCallback(stopCallback)
        {

            debugLog<MinimalOutput>("%s", "constructing LockedData");
            procStatm = open("/proc/self/statm", O_RDONLY);
            if (procStatm == -1) {
                fprintf(stderr, "WARNING: Failed to open /proc/self/statm for reading: %s.\n", strerror(errno));
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

                    try {
                        HeapTrack heaptrack([&] { return !stopTimerThread.load(); });
                        heaptrack.writeTimestamp();
                        heaptrack.writeRSS();
                    } catch (LockCheckFailed) {
                        break;
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
                } catch (const std::system_error&) {
                }
            }

            out.close();

            if (procStatm != -1) {
                close(procStatm);
            }

            if (stopCallback && (!s_atexit || s_forceCleanup)) {
                stopCallback();
            }
            debugLog<MinimalOutput>("%s", "done destroying LockedData");
        }

        LineWriter out;

        /// /proc/self/statm file descriptor to read RSS value from
        int procStatm = -1;

        /**
         * Calls to dlopen/dlclose mark the cache as dirty.
         * When this happened, all modules and their section addresses
         * must be found again via dl_iterate_phdr before we output the
         * next instruction pointer. Otherwise, heaptrack_interpret might
         * encounter IPs of an unknown/invalid module.
         */
        bool moduleCacheDirty = true;

        TraceTree traceTree;

        atomic<bool> stopTimerThread{false};
        thread timerThread;

        heaptrack_callback_t stopCallback = nullptr;

#ifdef DEBUG_MALLOC_PTRS
        unordered_set<void*> known;
#endif
    };

    static std::mutex s_lock;
    static LockedData* s_data;

private:
    static std::atomic<bool> s_paused;
};

std::mutex HeapTrack::s_lock;
HeapTrack::LockedData* HeapTrack::s_data{nullptr};
std::atomic<bool> HeapTrack::s_paused{false};
}

extern "C" {

void heaptrack_init(const char* outputFileName, heaptrack_callback_t initBeforeCallback,
                    heaptrack_callback_initialized_t initAfterCallback, heaptrack_callback_t stopCallback)
{
    RecursionGuard guard;
    // initialize
    startTime();

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

void heaptrack_pause()
{
    HeapTrack::setPaused(true);
}

void heaptrack_resume()
{
    HeapTrack::setPaused(false);
}

void heaptrack_malloc(void* ptr, size_t size)
{
    if (!HeapTrack::isPaused() && ptr && !RecursionGuard::isActive) {
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
    if (!HeapTrack::isPaused() && ptr && !RecursionGuard::isActive) {
        RecursionGuard guard;

        debugLog<VeryVerboseOutput>("heaptrack_free(%p)", ptr);

        HeapTrack heaptrack(guard);
        heaptrack.handleFree(ptr);
    }
}

void heaptrack_realloc(void* ptr_in, size_t size, void* ptr_out)
{
    if (!HeapTrack::isPaused() && ptr_out && !RecursionGuard::isActive) {
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
