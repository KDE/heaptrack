#include <thread>
#include <atomic>
#include <iostream>
#include <cstdlib>

#include <malloc.h>

#include "dumpmallocinfo.h"

using namespace std;

unique_ptr<thread> runner;
atomic_bool stop{false};

void dump_malloc_info(unsigned int millisecond_interval)
{
    while(!stop) {
        malloc_info(0, stderr);
        this_thread::sleep_for(chrono::milliseconds(millisecond_interval));
    }
    malloc_info(0, stderr);
}

void start_dump_malloc_info(unsigned int millisecond_interval)
{
    cerr << "Will dump malloc info every " << millisecond_interval << "ms" << endl;
    malloc_info(0, stderr);
    if (runner) {
        stop_dump_malloc_info();
    }
    stop = false;
    runner.reset({new thread(dump_malloc_info, millisecond_interval)});
}

void stop_dump_malloc_info()
{
    if (!runner) {
        return;
    }

    stop = 1;
    if (runner->joinable()) {
        runner->join();
    }
    runner.reset(0);
    cerr << "Stopped dump malloc info" << endl;
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
