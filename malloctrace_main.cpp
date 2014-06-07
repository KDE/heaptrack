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
#include <map>
#include <vector>
#include <memory>
#include <tuple>
#include <algorithm>

#include <cxxabi.h>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/algorithm/string/predicate.hpp>

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
    Module(string _fileName, bool isExe, uintptr_t addressStart, uintptr_t addressEnd)
        : fileName(move(_fileName))
        , addressStart(addressStart)
        , addressEnd(addressEnd)
        , isExe(isExe)
    {
        backtraceState = backtrace_create_state(fileName.c_str(), /* we are single threaded, so: not thread safe */ false,
                                                [] (void *data, const char *msg, int errnum) {
                                                    const Module* module = reinterpret_cast<Module*>(data);
                                                    cerr << "Failed to create backtrace state for file " << module->fileName
                                                         << ": " << msg << " (error code " << errnum << ")" << endl;
                                                }, this);

        if (backtraceState) {
            backtrace_fileline_initialize(backtraceState, addressStart, isExe,
                                          [] (void *data, const char *msg, int errnum) {
                                            const Module* module = reinterpret_cast<Module*>(data);
                                            cerr << "Failed to initialize backtrace fileline for "
                                                 << (module->isExe ? "executable" : "library") << module->fileName
                                                 << ": " << msg << " (error code " << errnum << ")" << endl;
                                          }, this);
        }
    }

    AddressInformation resolveAddress(uintptr_t address) const
    {
        AddressInformation info;
        if (!backtraceState) {
            return info;
        }

        backtrace_pcinfo(backtraceState, address,
                         [] (void *data, uintptr_t /*addr*/, const char *file, int line, const char *function) -> int {
                            auto info = reinterpret_cast<AddressInformation*>(data);
                            info->function = demangle(function);
                            info->file = file ? file : "";
                            info->line = line;
                            return 0;
                         }, &emptyErrorCallback, &info);

        if (info.function.empty()) {
            backtrace_syminfo(backtraceState, address,
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

    bool operator<(const Module& module) const
    {
        return make_tuple(addressStart, addressEnd, fileName)
             < make_tuple(module.addressStart, module.addressEnd, module.fileName);
    }

    bool operator!=(const Module& module) const
    {
        return make_tuple(addressStart, addressEnd, fileName)
            != make_tuple(module.addressStart, module.addressEnd, module.fileName);
    }

    backtrace_state* backtraceState = nullptr;
    string fileName;
    uintptr_t addressStart;
    uintptr_t addressEnd;
    bool isExe;
};

struct InstructionPointer
{
    uintptr_t instructionPointer;
    size_t parentIndex;
};

struct Allocation
{
    // backtrace entry point
    size_t ipIndex;
    // number of allocations
    size_t allocations;
    // amount of bytes leaked
    size_t leaked;
};

/**
 * Information for a single call to an allocation function
 */
struct AllocationInfo
{
    size_t ipIndex;
    size_t size;
};

struct AccumulatedTraceData
{
    AccumulatedTraceData()
    {
        modules.reserve(64);
        instructionPointers.reserve(65536);
        // root node with invalid instruction pointer
        instructionPointers.push_back({0, 0});
        allocations.reserve(16384);
        activeAllocations.reserve(65536);
    }

    void printBacktrace(InstructionPointer ip, ostream& out) const
    {
        while (ip.instructionPointer) {
            out << "0x" << hex << ip.instructionPointer << dec;
            // find module for this instruction pointer
            auto module = lower_bound(modules.begin(), modules.end(), ip.instructionPointer,
                                      [] (const Module& module, const uintptr_t ip) -> bool {
                                        return module.addressEnd < ip;
                                      });
            if (module != modules.end()
                && module->addressStart <= ip.instructionPointer
                && module->addressEnd >= ip.instructionPointer)
            {
                const auto info = module->resolveAddress(ip.instructionPointer);
                out << ' ' << info
                    << ' ' << module->fileName;
                if (info.function == "__libc_start_main") {
                    // hide anything above as its mostly garbage anyways
                    ip.parentIndex = 0;
                }
            } else {
                out << " <unknown module>";
            }
            out << endl;

            ip = instructionPointers[ip.parentIndex];
        };
    }

    Allocation& findAllocation(const size_t ipIndex)
    {
        auto it = lower_bound(allocations.begin(), allocations.end(), ipIndex,
                                [] (const Allocation& allocation, const size_t ipIndex) -> bool {
                                    return allocation.ipIndex < ipIndex;
                                });
        if (it == allocations.end() || it->ipIndex != ipIndex) {
            it = allocations.insert(it, {ipIndex, 0, 0});
        }
        return *it;
    }

    InstructionPointer findIp(const size_t ipIndex) const
    {
        if (ipIndex > instructionPointers.size()) {
            return {};
        } else {
            return instructionPointers[ipIndex];
        }
    }

    vector<Module> modules;
    vector<InstructionPointer> instructionPointers;
    vector<Allocation> allocations;
    unordered_map<uintptr_t, AllocationInfo> activeAllocations;

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

    string fileName(argv[1]);
    const bool isCompressed = boost::algorithm::ends_with(fileName, ".gz");
    ifstream file(fileName, isCompressed ? ios_base::in | ios_base::binary : ios_base::in);

    if (!file.is_open()) {
        cerr << "Failed to open malloctrace log file: " << argv[1] << endl;
        cerr << endl;
        printUsage(cerr);
        return 1;
    }

    boost::iostreams::filtering_istream in;
    if (isCompressed) {
        in.push(boost::iostreams::gzip_decompressor());
    }
    in.push(file);

    string line;
    line.reserve(1024);
    stringstream lineIn(ios_base::in);
    lineIn << hex;
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
            string fileName;
            lineIn >> fileName;
            bool isExe = false;
            lineIn >> isExe;
            uintptr_t addressStart = 0;
            lineIn >> addressStart;
            uintptr_t addressEnd = 0;
            lineIn >> addressEnd;
            if (lineIn.bad()) {
                cerr << "failed to parse line: " << line << endl;
                return 1;
            }
            data.modules.push_back({fileName, isExe, addressStart, addressEnd});
        } else if (mode == 'i') {
            InstructionPointer ip{0, 0};
            lineIn >> ip.instructionPointer;
            lineIn >> ip.parentIndex;
            if (lineIn.bad()) {
                cerr << "failed to parse line: " << line << endl;
                return 1;
            }
            data.instructionPointers.push_back(ip);
        } else if (mode == '+') {
            size_t size = 0;
            lineIn >> size;
            size_t ipId = 0;
            lineIn >> ipId;
            uintptr_t ptr = 0;
            lineIn >> ptr;
            if (lineIn.bad()) {
                cerr << "failed to parse line: " << line << endl;
                return 1;
            }

            data.activeAllocations[ptr] = {ipId, size};

            auto& allocation = data.findAllocation(ipId);
            allocation.leaked += size;
            ++allocation.allocations;
            data.totalAllocated += size;
            ++data.totalAllocations;
            data.leaked += size;
            if (data.leaked > data.peak) {
                data.peak = data.leaked;
            }
            ++data.sizeHistogram[size];
        } else if (mode == '-') {
            uintptr_t ptr = 0;
            lineIn >> ptr;
            if (lineIn.bad()) {
                cerr << "failed to parse line: " << line << endl;
                return 1;
            }
            auto ip = data.activeAllocations.find(ptr);
            if (ip == data.activeAllocations.end()) {
                cerr << "unknown pointer in line: " << line << endl;
                continue;
            }
            const auto info = ip->second;
            data.activeAllocations.erase(ip);

            auto& allocation = data.findAllocation(info.ipIndex);
            if (!allocation.allocations || allocation.leaked < info.size) {
                cerr << "inconsistent allocation info, underflowed allocations of " << info.ipIndex << endl;
                allocation.leaked = 0;
                allocation.allocations = 0;
            } else {
                allocation.leaked -= info.size;
            }
            data.leaked -= info.size;
        } else {
            cerr << "failed to parse line: " << line << endl;
        }
    }

    // sort by addresses
    sort(data.modules.begin(), data.modules.end());

    // sort by amount of allocations
    sort(data.allocations.begin(), data.allocations.end(), [] (const Allocation& l, const Allocation &r) {
        return l.allocations > r.allocations;
    });
    cout << "TOP ALLOCATORS" << endl;
    for (size_t i = 0; i < min(10lu, data.allocations.size()); ++i) {
        const auto& allocation = data.allocations[i];
        cout << allocation.allocations << " allocations at:" << endl;
        data.printBacktrace(data.findIp(allocation.ipIndex), cout);
        cout << endl;
    }
    cout << endl;

    // sort by amount of leaks
    sort(data.allocations.begin(), data.allocations.end(), [] (const Allocation& l, const Allocation &r) {
        return l.leaked < r.leaked;
    });

    size_t totalLeakAllocations = 0;
    for (const auto& allocation : data.allocations) {
        if (!allocation.leaked) {
            continue;
        }
        totalLeakAllocations += allocation.allocations;

        cout << allocation.leaked << " bytes leaked in " << allocation.allocations << " allocations at:" << endl;
        data.printBacktrace(data.findIp(allocation.ipIndex), cout);
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
