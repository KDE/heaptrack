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
#include <unordered_map>
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
    if (!function || function[0] != '_' || function[1] != 'Z') {
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

    string resolveAddress(uintptr_t offset) const
    {
        string ret;
        if (!backtraceState) {
            return ret;
        }
        backtrace_syminfo(backtraceState, baseAddress + offset,
                          [] (void *data, uintptr_t /*pc*/, const char *symname, uintptr_t /*symval*/, uintptr_t /*symsize*/) {
                            if (symname) {
                                *reinterpret_cast<string*>(data) = demangle(symname);
                            }
                          }, &errorCallback, &ret);
        if (ret.empty()) {
            ret = "??";
        }
        return ret;
    }

    static void errorCallback(void */*data*/, const char *msg, int errnum)
    {
        cerr << "Module backtrace error (code " << errnum << "): " << msg << endl;
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

    /// TODO: vectors?
    unordered_map<unsigned int, shared_ptr<Module>> modules;
    unordered_map<unsigned int, InstructionPointer> instructions;
    vector<Trace> traces;
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
    unsigned int nextTraceId = 0;
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
            unsigned int id = 0;
            lineIn >> id;
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
                continue;
            }
            data.modules[id] = make_shared<Module>(fileName, baseAddress, isExe);
        } else if (mode == 'i') {
            InstructionPointer ip;
            unsigned int id = 0;
            lineIn >> id;
            unsigned int moduleId = 0;
            lineIn >> moduleId;
            lineIn << hex;
            lineIn >> ip.offset;
            lineIn << dec;
            if (lineIn.bad()) {
                cerr << "failed to parse line: " << line << endl;
                continue;
            }
            auto module = data.modules.find(moduleId);
            if (module != data.modules.end()) {
                ip.module = module->second;
            }
            data.instructions[id] = ip;
        } else if (mode == 't') {
            Trace trace;
            unsigned int id = 0;
            lineIn >> id;
            if (lineIn.bad()) {
                cerr << "failed to parse line: " << line << endl;
                continue;
            }
            if (id != nextTraceId) {
                cerr << "inconsistent trace data: " << line << endl << "expected trace with id: " << nextTraceId << endl;
                return 1;
            }
            while (lineIn.good()) {
                unsigned int ipId = 0;
                lineIn >> ipId;
                auto ip = data.instructions.find(ipId);
                if (ip != data.instructions.end()) {
                    trace.backtrace.push_back(ip->second);
                } else {
                    cerr << "failed to find instruction " << ipId << endl;
                }
            }
            if (data.traces.size() <= id) {
                data.traces.reserve(id + 1000);
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
                continue;
            }
            if (traceId < data.traces.size()) {
                auto& trace = data.traces[traceId];
                trace.leaked += size;
                trace.allocations++;
            } else {
                cerr << "failed to find trace of malloc at " << traceId << endl;
            }
        } else if (mode == '-') {
            /// TODO
            size_t size = 0;
            lineIn >> size;
            unsigned int traceId = 0;
            lineIn >> traceId;
            if (lineIn.bad()) {
                cerr << "failed to parse line: " << line << endl;
                continue;
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

    for (const auto& trace : data.traces) {
        if (!trace.leaked) {
            continue;
        }
        cout << trace.leaked << " bytes leaked in " << trace.allocations << " allocations at:" << endl;
        trace.printBacktrace(cout);
        cout << endl;
    }

    return 0;
}
