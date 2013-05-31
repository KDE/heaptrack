/*
 * Copyright 2013 Milian Wolff <mail@milianw.de>
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

#include <thread>
#include <atomic>
#include <iostream>
#include <cstdlib>
#include <chrono>

#include <malloc.h>
#include <unistd.h>

#include "dumpmallocinfo.h"

using namespace std;

thread* runner = 0;
atomic_bool stop{false};

void dump_malloc_info(FILE* output)
{
    static unsigned long id = 0;

    if (id == 0) {
        fprintf(output, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
        //TODO: add info about program being investigated
        fprintf(output, "<mallocinfo cmd=\"unknown\" descr=\"\">\n");
    }

    auto duration = chrono::system_clock::now().time_since_epoch();
    auto millis = chrono::duration_cast<chrono::milliseconds>(duration).count();

    fprintf(output, "<snapshot id=\"%lu\" time=\"%lu\">\n", id++, millis);
    malloc_info(0, output);
    ///TODO: the above does not contain information about what is actually in use :-/
    ///      fallback to int-based interface until this gets added upstream
    auto info = mallinfo();
    fprintf(output, "<mallinfo used=\"%d\" mmap=\"%d\" kept=\"%d\"/>\n",
            info.uordblks, info.hblkhd, info.keepcost);
    fprintf(output, "</snapshot>\n");
}

void thread_dump_malloc_info(FILE* output, unsigned int millisecond_interval)
{
    while(!stop) {
        dump_malloc_info(output);
        this_thread::sleep_for(chrono::milliseconds(millisecond_interval));
    }
    // dump one last frame before going back to the main thread
    dump_malloc_info(output);
}

void start_dump_malloc_info(FILE* output, unsigned int millisecond_interval)
{
    if (runner) {
        cerr << "malloc info is still running, stop it first before restarting it" << endl;
        return;
    }
    // dump an early first frame before starting up the thread
    dump_malloc_info(output);

    stop = false;
    runner = new thread(thread_dump_malloc_info, output, millisecond_interval);
}

void stop_dump_malloc_info(FILE* output)
{
    if (!runner) {
        return;
    }

    stop = true;
    if (runner->joinable()) {
        runner->join();
    }
    delete runner;
    runner = 0;
    fprintf(output, "</mallocinfo>\n");
}

string env(const char* variable)
{
    const char* value = getenv(variable);
    return value ? string(value) : string();
}

DumpMallocInfoOnStartup::DumpMallocInfoOnStartup()
: output(0)
{
    // don't trace child apps
    setenv("LD_PRELOAD", "", 1);

    unsigned int ms_interval = 0;
    try {
        ms_interval = stoul(env("DUMP_MALLOC_INFO_INTERVAL"));
    } catch(...) {
        cerr << "unsigned integer expected for DUMP_MALLOC_INFO_INTERVAL env variable, not dumping anything now" << endl;
        return;
    }

    if (ms_interval == 0) {
        return;
    }

    const string outputType = env("DUMP_MALLOC_INFO_OUTPUT");
    if (outputType.empty() || outputType == "stderr") {
        output = stderr;
    } else if (outputType == "stdout") {
        output = stdout;
    } else {
        string outputFileName = outputType + '.' + std::to_string(getpid());
        output = fopen(outputFileName.c_str(), "w+");
        if (!output) {
            cerr << "Cannot open file " << outputFileName << " for writing" << endl;
            return;
        }
    }

    start_dump_malloc_info(output, ms_interval);
}

DumpMallocInfoOnStartup::~DumpMallocInfoOnStartup()
{
    stop_dump_malloc_info(output);

    if (output && output != stderr && output != stdout) {
        fclose(output);
    }
}
