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

#include "dumpmallocinfo.h"

using namespace std;

thread* runner = 0;
atomic_bool stop{false};

void dump_malloc_info()
{
    ///TODO: make output destination configurable
    FILE* output = stderr;

    static unsigned long id = 0;

    auto duration = chrono::system_clock::now().time_since_epoch();
    auto millis = chrono::duration_cast<chrono::milliseconds>(duration).count();

    fprintf(output, "<snapshot id=\"%lu\" time=\"%lu\">\n", id++, millis);
    malloc_info(0, output);
    fprintf(output, "</snapshot>\n");
}

void thread_dump_malloc_info(unsigned int millisecond_interval)
{
    while(!stop) {
        dump_malloc_info();
        this_thread::sleep_for(chrono::milliseconds(millisecond_interval));
    }
    // dump one last frame before going back to the main thread
    dump_malloc_info();
}

void start_dump_malloc_info(unsigned int millisecond_interval)
{
    // dump an early first frame before starting up the thread
    dump_malloc_info();

    if (runner) {
        stop_dump_malloc_info();
    }
    stop = false;
    runner = new thread(thread_dump_malloc_info, millisecond_interval);
}

void stop_dump_malloc_info()
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
}

DumpMallocInfoOnStartup::DumpMallocInfoOnStartup()
{
    const char* envVar = getenv("DUMP_MALLOC_INFO_INTERVAL");
    if (!envVar) {
        cerr << "DUMP_MALLOC_INFO_INTERVAL env var not set, not dumping malloc info" << endl;
        return;
    }

    unsigned int ms_interval = 0;
    try {
        ms_interval = stoul(string(envVar));
    } catch(...) {
        cerr << "unsigned integer expected for DUMP_MALLOC_INFO_INTERVAL env variable" << endl;
        return;
    }

    if (ms_interval == 0) {
        return;
    }

    start_dump_malloc_info(ms_interval);
}

DumpMallocInfoOnStartup::~DumpMallocInfoOnStartup()
{
    stop_dump_malloc_info();
}
