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
    unordered_map<unsigned int, Trace> traces;
};

}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printUsage(cerr);
        return 1;
    }

    AccumulatedTraceData data;

    for (int i = 1; i < argc; ++i) {
        fstream in(argv[i], ios_base::in);
        if (!in.is_open()) {
            cerr << "Failed to open malloctrace log file: " << argv[1] << endl;
            cerr << endl;
            printUsage(cerr);
            return 1;
        }

        string line;
        line.reserve(1024);
        stringstream lineIn(ios_base::in);
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
                data.traces[id] = trace;
            } else if (mode == '+') {
                size_t size = 0;
                lineIn >> size;
                unsigned int traceId = 0;
                lineIn >> traceId;
                if (lineIn.bad()) {
                    cerr << "failed to parse line: " << line << endl;
                    continue;
                }
                auto trace = data.traces.find(traceId);
                if (trace != data.traces.end()) {
                    trace->second.leaked += size;
                    trace->second.allocations++;
                } else {
                    cerr << "failed to find trace " << traceId << endl;
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
                auto trace = data.traces.find(traceId);
                if (trace != data.traces.end()) {
                    if (trace->second.leaked >= size) {
                        trace->second.leaked -= size;
                    } else {
                        cerr << "inconsistent allocation info, underflowed allocations of " << traceId << endl;
                        trace->second.leaked = 0;
                    }
                } else {
                    cerr << "failed to find trace " << traceId << endl;
                }
            } else {
                cerr << "failed to parse line: " << line << endl;
            }
        }
    }

    for (const auto& trace : data.traces) {
        if (!trace.second.leaked) {
            continue;
        }
        cout << trace.second.leaked << " leaked in: " << trace.first << " allocations: " << trace.second.allocations << endl;
    }

    return 0;
}
