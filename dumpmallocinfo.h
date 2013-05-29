#ifndef DUMPMALLOCINFO_H
#define DUMPMALLOCINFO_H

extern "C" {
void start_dump_malloc_info(unsigned int millisecond_interval);
void stop_dump_malloc_info();
}

struct DumpMallocInfoOnStartup
{
    DumpMallocInfoOnStartup();
    ~DumpMallocInfoOnStartup();
};

static DumpMallocInfoOnStartup dumpMallocInfoOnStartup;

#endif // DUMPMALLOCINFO_H
