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

/**
 * @file heaptrack_interpret.cpp
 *
 * @brief Interpret raw heaptrack data and add Dwarf based debug information.
 */

#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <tuple>
#include <algorithm>

#include <cxxabi.h>

#include "libbacktrace/backtrace.h"

using namespace std;

namespace {

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

struct ResolvedIP
{
    size_t moduleIndex = 0;
    size_t fileIndex = 0;
    size_t functionIndex = 0;
    int line = 0;
};

struct AccumulatedTraceData
{
    AccumulatedTraceData()
    {
        m_modules.reserve(64);
        m_internedData.reserve(4096);
        m_encounteredIps.reserve(32768);
    }

    ~AccumulatedTraceData()
    {
        cout << dec;
        cout << "# strings: " << m_internedData.size() << '\n';
        cout << "# ips: " << m_encounteredIps.size() << '\n';
    }

    ResolvedIP resolve(const uintptr_t ip)
    {
        if (m_modulesDirty) {
            // sort by addresses, required for binary search below
            sort(m_modules.begin(), m_modules.end());
            m_modulesDirty = false;
        }

        ResolvedIP data;
        // find module for this instruction pointer
        auto module = lower_bound(m_modules.begin(), m_modules.end(), ip,
                                    [] (const Module& module, const uintptr_t ip) -> bool {
                                        return module.addressEnd < ip;
                                    });
        if (module != m_modules.end() && module->addressStart <= ip && module->addressEnd >= ip) {
            data.moduleIndex = intern(module->fileName);
            const auto info = module->resolveAddress(ip);
            data.fileIndex = intern(info.file);
            data.functionIndex = intern(info.function);
            data.line = info.line;
        }
        return data;
    }

    size_t intern(const string& str)
    {
        if (str.empty()) {
            return 0;
        }

        auto it = m_internedData.find(str);
        if (it != m_internedData.end()) {
            return it->second;
        }
        const size_t id = m_nextInternId++;
        m_internedData.insert(it, make_pair(str, id));
        cout << "s " << str << '\n';
        return id;
    }

    void addModule(Module&& module)
    {
        m_modules.push_back(move(module));
        m_modulesDirty = true;
    }

    size_t addIp(const uintptr_t instructionPointer)
    {
        if (!instructionPointer) {
            return 0;
        }

        auto it = m_encounteredIps.find(instructionPointer);
        if (it != m_encounteredIps.end()) {
            return it->second;
        }

        const size_t ipId = m_nextIpId++;
        m_encounteredIps.insert(it, make_pair(instructionPointer, ipId));

        const auto ip = resolve(instructionPointer);
        cout << "i " << instructionPointer << ' ' << ip.moduleIndex;
        if (ip.functionIndex || ip.fileIndex) {
            cout << ' ' << ip.functionIndex;
            if (ip.fileIndex) {
                cout << ' ' << ip.fileIndex << ' ' << ip.line;
            }
        }
        cout << '\n';
        return ipId;
    }

private:
    vector<Module> m_modules;
    bool m_modulesDirty = false;

    size_t m_nextInternId = 1;
    unordered_map<string, size_t> m_internedData;
    size_t m_nextIpId = 1;
    unordered_map<uintptr_t, size_t> m_encounteredIps;
};

}

int main(int /*argc*/, char** /*argv*/)
{
    // optimize: we only have a single thread
    ios_base::sync_with_stdio(false);

    AccumulatedTraceData data;

    string line;
    line.reserve(1024);
    stringstream lineIn(ios_base::in);
    lineIn << hex;
    cout << hex;

    while (cin.good()) {
        getline(cin, line);
        if (line.empty()) {
            continue;
        }
        const char mode = line[0];
        lineIn.str(line);
        lineIn.clear();
        // skip mode and leading whitespace
        lineIn.seekg(2);
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
            data.addModule({fileName, isExe, addressStart, addressEnd});
        } else if (mode == 't') {
            uintptr_t instructionPointer = 0;
            size_t parentIndex = 0;
            lineIn >> instructionPointer;
            lineIn >> parentIndex;
            if (lineIn.bad()) {
                cerr << "failed to parse line: " << line << endl;
                return 1;
            }
            // ensure ip is encountered
            const auto ipId = data.addIp(instructionPointer);
            // trace point, map current output index to parent index
            cout << "t " << ipId << ' ' << parentIndex << '\n';
        } else {
            cout << line << '\n';
        }
    }

    return 0;
}
