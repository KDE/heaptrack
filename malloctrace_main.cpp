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

#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <memory>
#include <cxxabi.h>
#include <algorithm>

#include "libbacktrace/backtrace.h"

using namespace std;

namespace {

void printUsage(ostream& out)
{
    out << "malloctrace_main MALLOCTRACE_LOG_FILE..." << endl;
}

string demangle(const char* function)
{
    if (!function) {
        return {};
    } else if (function[0] != '_' || function[1] != 'Z') {
        return {function};
    }

    string ret;
    int status = 0;
    char* demangled = abi::__cxa_demangle(function, 0, 0, &status);
    if (demangled) {
        ret = demangled;
        free(demangled);
    }
    return ret;
}

struct AddressInformation
{
    string function;
    string file;
    int line = 0;
};

ostream& operator<<(ostream& out, const AddressInformation& info)
{
    out << info.function;
    if (!info.file.empty()) {
        out << " in " << info.file << ':' << info.line;
    }
    return out;
}

struct Module
{
    Module(string _fileName, uintptr_t baseAddress, bool isExe)
        : fileName(move(_fileName))
        , baseAddress(baseAddress)
        , isExe(isExe)
    {
        backtraceState = backtrace_create_state(fileName.c_str(), /* we are single threaded, so: not thread safe */ false,
                                                [] (void *data, const char *msg, int errnum) {
                                                    const Module* module = reinterpret_cast<Module*>(data);
                                                    cerr << "Failed to create backtrace state for file " << module->fileName
                                                         << ": " << msg << " (error code " << errnum << ")" << endl;
                                                }, this);

        if (backtraceState) {
            backtrace_fileline_initialize(backtraceState, baseAddress, isExe,
                                          [] (void *data, const char *msg, int errnum) {
                                            const Module* module = reinterpret_cast<Module*>(data);
                                            cerr << "Failed to initialize backtrace fileline for "
                                                 << (module->isExe ? "executable" : "library") << module->fileName
                                                 << ": " << msg << " (error code " << errnum << ")" << endl;
                                        }, this);
        }
    }

    AddressInformation resolveAddress(uintptr_t offset) const
    {
        AddressInformation info;
        if (!backtraceState) {
            return info;
        }

        backtrace_pcinfo(backtraceState, baseAddress + offset,
                         [] (void *data, uintptr_t /*addr*/, const char *file, int line, const char *function) -> int {
                            auto info = reinterpret_cast<AddressInformation*>(data);
                            info->function = demangle(function);
                            info->file = file ? file : "";
                            info->line = line;
                            return 0;
                         }, &emptyErrorCallback, &info);
        if (info.function.empty()) {
            backtrace_syminfo(backtraceState, baseAddress + offset,
                              [] (void *data, uintptr_t /*pc*/, const char *symname, uintptr_t /*symval*/, uintptr_t /*symsize*/) {
                                if (symname) {
                                    reinterpret_cast<AddressInformation*>(data)->function = demangle(symname);
                                }
                              }, &errorCallback, &info);
        }

        if (info.function.empty()) {
            info.function = "?";
        }

        return info;
    }

    static void errorCallback(void */*data*/, const char *msg, int errnum)
    {
        cerr << "Module backtrace error (code " << errnum << "): " << msg << endl;
    }

    static void emptyErrorCallback(void */*data*/, const char */*msg*/, int /*errnum*/)
    {
    }

    backtrace_state* backtraceState = nullptr;
    string fileName;
    uintptr_t baseAddress;
    bool isExe;
};

struct InstructionPointer
{
    shared_ptr<Module> module;
    uintptr_t offset;
};

struct Trace
{
    vector<InstructionPointer> backtrace;
    size_t allocations = 0;
    size_t leaked = 0;

    void printBacktrace(ostream& out) const
    {
        for (const auto& ip : backtrace) {
            out << "0x" << hex << ip.offset << dec
                << ' ' << ip.module->resolveAddress(ip.offset)
                << ' ' << ip.module->fileName << endl;
        }
    }
};

struct AccumulatedTraceData
{
    AccumulatedTraceData()
    {
        modules.reserve(64);
        instructions.reserve(65536);
        traces.reserve(16384);
    }

    vector<shared_ptr<Module>> modules;
    vector<InstructionPointer> instructions;
    vector<Trace> traces;

    map<size_t, size_t> sizeHistogram;
    size_t totalAllocated = 0;
    size_t totalAllocations = 0;
    size_t peak = 0;
    size_t leaked = 0;
};

}

int main(int argc, char** argv)
{
    if (argc != 2) {
        printUsage(cerr);
        return 1;
    }

    AccumulatedTraceData data;

    fstream in(argv[1], ios_base::in);
    if (!in.is_open()) {
        cerr << "Failed to open malloctrace log file: " << argv[1] << endl;
        cerr << endl;
        printUsage(cerr);
        return 1;
    }

    string line;
    line.reserve(1024);
    stringstream lineIn(ios_base::in);
    size_t nextTraceId = 0;
    size_t nextModuleId = 0;
    size_t nextIpId = 0;
    while (in.good()) {
        getline(in, line);
        if (line.empty()) {
            continue;
        }
        lineIn.str(line);
        lineIn.clear();
        char mode = 0;
        lineIn >> mode;
        if (mode == 'm') {
            size_t id = 0;
            lineIn >> id;
            if (id != nextModuleId) {
                cerr << "inconsistent trace data: " << line << endl
                     << "expected module with id: " << nextModuleId << endl;
                return 1;
            }
            string fileName;
            lineIn >> fileName;
            lineIn << hex;
            uintptr_t baseAddress = 0;
            lineIn >> baseAddress;
            bool isExe = false;
            lineIn << dec;
            lineIn >> isExe;
            if (lineIn.bad()) {
                cerr << "failed to parse line: " << line << endl;
                return 1;
            }
            data.modules.push_back(make_shared<Module>(fileName, baseAddress, isExe));
            ++nextModuleId;
        } else if (mode == 'i') {
            InstructionPointer ip;
            size_t id = 0;
            lineIn >> id;
            if (id != nextIpId) {
                cerr << "inconsistent trace data: " << line << endl
                     << "expected instruction with id: " << nextIpId << endl;
                return 1;
            }

            size_t moduleId = 0;
            lineIn >> moduleId;
            if (moduleId >= data.modules.size()) {
                cerr << "inconsistent trace data: " << line << endl
                     << "failed to find module " << moduleId << ", only got so far: " << data.modules.size() << endl;
                return 1;
            }

            lineIn << hex;
            lineIn >> ip.offset;
            lineIn << dec;
            if (lineIn.bad()) {
                cerr << "failed to parse line: " << line << endl;
                return 1;
            }
            ip.module = data.modules[moduleId];
            data.instructions.push_back(ip);
            ++nextIpId;
        } else if (mode == 't') {
            Trace trace;
            unsigned int id = 0;
            lineIn >> id;
            if (lineIn.bad()) {
                cerr << "failed to parse line: " << line << endl;
                return 1;
            }
            if (id != nextTraceId) {
                cerr << "inconsistent trace data: " << line << endl
                     << "expected trace with id: " << nextTraceId << endl;
                return 1;
            }
            while (lineIn.good()) {
                unsigned int ipId = 0;
                lineIn >> ipId;
                if (ipId >= data.instructions.size()) {
                    cerr << "inconsistent trace data: " << line << endl
                         << "failed to find instruction " << ipId << endl;
                    return 1;
                }
                trace.backtrace.push_back(data.instructions[ipId]);
            }
            data.traces.push_back(trace);
            ++nextTraceId;
        } else if (mode == '+') {
            size_t size = 0;
            lineIn >> size;
            unsigned int traceId = 0;
            lineIn >> traceId;
            if (lineIn.bad()) {
                cerr << "failed to parse line: " << line << endl;
                return 1;
            }
            if (traceId < data.traces.size()) {
                auto& trace = data.traces[traceId];
                trace.leaked += size;
                ++trace.allocations;
            } else {
                cerr << "failed to find trace of malloc at " << traceId << endl;
                return 1;
            }
            data.totalAllocated += size;
            data.totalAllocations++;
            data.leaked += size;
            if (data.leaked > data.peak) {
                data.peak = data.leaked;
            }
            data.sizeHistogram[size]++;
        } else if (mode == '-') {
            /// TODO
            size_t size = 0;
            lineIn >> size;
            unsigned int traceId = 0;
            lineIn >> traceId;
            if (lineIn.bad()) {
                cerr << "failed to parse line: " << line << endl;
                return 1;
            }
            if (traceId < data.traces.size()) {
                auto& trace = data.traces[traceId];
                if (trace.leaked >= size) {
                    trace.leaked -= size;
                } else {
                    cerr << "inconsistent allocation info, underflowed allocations of " << traceId << endl;
                    trace.leaked = 0;
                }
            } else {
                cerr << "failed to find trace for free at " << traceId << endl;
            }
            data.leaked -= size;
        } else {
            cerr << "failed to parse line: " << line << endl;
        }
    }

    // sort by amount of allocations
    sort(data.traces.begin(), data.traces.end(), [] (const Trace& l, const Trace &r) {
        return l.allocations > r.allocations;
    });
    cout << "TOP ALLOCATORS" << endl;
    for (size_t i = 0; i < min(10lu, data.traces.size()); ++i) {
        const auto& trace = data.traces[i];
        cout << trace.allocations << " allocations at:" << endl;
        trace.printBacktrace(cout);
        cout << endl;
    }
    cout << endl;


    // sort by amount of leaks
    sort(data.traces.begin(), data.traces.end(), [] (const Trace& l, const Trace &r) {
        return l.leaked < r.leaked;
    });

    size_t totalLeakAllocations = 0;
    for (const auto& trace : data.traces) {
        if (!trace.leaked) {
            continue;
        }
        totalLeakAllocations += trace.allocations;

        cout << trace.leaked << " bytes leaked in " << trace.allocations << " allocations at:" << endl;
        trace.printBacktrace(cout);
        cout << endl;
    }
    cout << data.leaked << " bytes leaked in total from " << totalLeakAllocations << " allocations" << endl;

    cout << data.totalAllocated << " bytes allocated in total over " << data.totalAllocations
         << " allocations, peak consumption: " << data.peak << " bytes" << endl;

    cout << endl;

    cout << "size histogram: " << endl;
    for (auto entry : data.sizeHistogram) {
        cout << entry.first << "\t" << entry.second << endl;
    }

    return 0;
}
