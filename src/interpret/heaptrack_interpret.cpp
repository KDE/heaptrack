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
 * @file heaptrack_interpret.cpp
 *
 * @brief Interpret raw heaptrack data and add Dwarf based debug information.
 */

#include <algorithm>
#include <cinttypes>
#include <iostream>
#include <sstream>
#include <stdio_ext.h>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <cxxabi.h>

#include <boost/algorithm/string/predicate.hpp>

#include "libbacktrace/backtrace.h"
#include "libbacktrace/internal.h"
#include "util/linereader.h"
#include "util/pointermap.h"

#include <unistd.h>

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
    Module(uintptr_t addressStart, uintptr_t addressEnd, backtrace_state* backtraceState, size_t moduleIndex)
        : addressStart(addressStart)
        , addressEnd(addressEnd)
        , moduleIndex(moduleIndex)
        , backtraceState(backtraceState)
    {
    }

    AddressInformation resolveAddress(uintptr_t address) const
    {
        AddressInformation info;
        if (!backtraceState) {
            return info;
        }

        backtrace_pcinfo(backtraceState, address,
                         [](void* data, uintptr_t /*addr*/, const char* file, int line, const char* function) -> int {
                             auto info = reinterpret_cast<AddressInformation*>(data);
                             info->function = demangle(function);
                             info->file = file ? file : "";
                             info->line = line;
                             return 0;
                         },
                         [](void* /*data*/, const char* /*msg*/, int /*errnum*/) {}, &info);

        if (info.function.empty()) {
            backtrace_syminfo(
                backtraceState, address,
                [](void* data, uintptr_t /*pc*/, const char* symname, uintptr_t /*symval*/, uintptr_t /*symsize*/) {
                    if (symname) {
                        reinterpret_cast<AddressInformation*>(data)->function = demangle(symname);
                    }
                },
                [](void* /*data*/, const char* msg, int errnum) {
                    cerr << "Module backtrace error (code " << errnum << "): " << msg << endl;
                },
                &info);
        }

        return info;
    }

    bool operator<(const Module& module) const
    {
        return tie(addressStart, addressEnd, moduleIndex)
            < tie(module.addressStart, module.addressEnd, module.moduleIndex);
    }

    bool operator!=(const Module& module) const
    {
        return tie(addressStart, addressEnd, moduleIndex)
            != tie(module.addressStart, module.addressEnd, module.moduleIndex);
    }

    uintptr_t addressStart;
    uintptr_t addressEnd;
    size_t moduleIndex;
    backtrace_state* backtraceState;
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
        m_modules.reserve(256);
        m_backtraceStates.reserve(64);
        m_internedData.reserve(4096);
        m_encounteredIps.reserve(32768);
    }

    ~AccumulatedTraceData()
    {
        fprintf(stdout, "# strings: %zu\n# ips: %zu\n", m_internedData.size(), m_encounteredIps.size());
    }

    ResolvedIP resolve(const uintptr_t ip)
    {
        if (m_modulesDirty) {
            // sort by addresses, required for binary search below
            sort(m_modules.begin(), m_modules.end());

#ifndef NDEBUG
            for (size_t i = 0; i < m_modules.size(); ++i) {
                const auto& m1 = m_modules[i];
                for (size_t j = i + 1; j < m_modules.size(); ++j) {
                    if (i == j) {
                        continue;
                    }
                    const auto& m2 = m_modules[j];
                    if ((m1.addressStart <= m2.addressStart && m1.addressEnd > m2.addressStart)
                        || (m1.addressStart < m2.addressEnd && m1.addressEnd >= m2.addressEnd)) {
                        cerr << "OVERLAPPING MODULES: " << hex << m1.moduleIndex << " (" << m1.addressStart << " to "
                             << m1.addressEnd << ") and " << m1.moduleIndex << " (" << m2.addressStart << " to "
                             << m2.addressEnd << ")\n"
                             << dec;
                    } else if (m2.addressStart >= m1.addressEnd) {
                        break;
                    }
                }
            }
#endif

            m_modulesDirty = false;
        }

        ResolvedIP data;
        // find module for this instruction pointer
        auto module =
            lower_bound(m_modules.begin(), m_modules.end(), ip,
                        [](const Module& module, const uintptr_t ip) -> bool { return module.addressEnd < ip; });
        if (module != m_modules.end() && module->addressStart <= ip && module->addressEnd >= ip) {
            data.moduleIndex = module->moduleIndex;
            const auto info = module->resolveAddress(ip);
            data.fileIndex = intern(info.file);
            data.functionIndex = intern(info.function);
            data.line = info.line;
        }
        return data;
    }

    size_t intern(const string& str, std::string* internedString = nullptr)
    {
        if (str.empty()) {
            return 0;
        }

        auto it = m_internedData.find(str);
        if (it != m_internedData.end()) {
            if (internedString) {
                *internedString = it->first;
            }
            return it->second;
        }
        const size_t id = m_internedData.size() + 1;
        it = m_internedData.insert(it, make_pair(str, id));
        if (internedString) {
            *internedString = it->first;
        }
        fprintf(stdout, "s %s\n", str.c_str());
        return id;
    }

    void addModule(backtrace_state* backtraceState, const size_t moduleIndex, const uintptr_t addressStart,
                   const uintptr_t addressEnd)
    {
        m_modules.emplace_back(addressStart, addressEnd, backtraceState, moduleIndex);
        m_modulesDirty = true;
    }

    void clearModules()
    {
        // TODO: optimize this, reuse modules that are still valid
        m_modules.clear();
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

        const size_t ipId = m_encounteredIps.size() + 1;
        m_encounteredIps.insert(it, make_pair(instructionPointer, ipId));

        const auto ip = resolve(instructionPointer);
        fprintf(stdout, "i %zx %zx", instructionPointer, ip.moduleIndex);
        if (ip.functionIndex || ip.fileIndex) {
            fprintf(stdout, " %zx", ip.functionIndex);
            if (ip.fileIndex) {
                fprintf(stdout, " %zx %x", ip.fileIndex, ip.line);
            }
        }
        fputc('\n', stdout);
        return ipId;
    }

    std::string findDebugFile(const std::string& input) const
    {
        // TODO: also try to find a debug file by build-id
        // TODO: also lookup in (user-configurable) debug path
        std::string file = input + ".debug";
        if (access(file.c_str(), R_OK) == 0) {
            return file;
        } else {
            return input;
        }
    }

    /**
     * Prevent the same file from being initialized multiple times.
     * This drastically cuts the memory consumption down
     */
    backtrace_state* findBacktraceState(const std::string& originalFileName, uintptr_t addressStart)
    {
        if (boost::algorithm::starts_with(originalFileName, "linux-vdso.so")) {
            // prevent warning, since this will always fail
            return nullptr;
        }

        auto it = m_backtraceStates.find(originalFileName);
        if (it != m_backtraceStates.end()) {
            return it->second;
        }

        const auto fileName = findDebugFile(originalFileName);

        struct CallbackData
        {
            const char* fileName;
        };
        CallbackData data = {fileName.c_str()};

        auto errorHandler = [](void* rawData, const char* msg, int errnum) {
            auto data = reinterpret_cast<const CallbackData*>(rawData);
            cerr << "Failed to create backtrace state for module " << data->fileName << ": " << msg << " / "
                 << strerror(errnum) << " (error code " << errnum << ")" << endl;
        };

        auto state = backtrace_create_state(data.fileName, /* we are single threaded, so: not thread safe */ false,
                                            errorHandler, &data);

        if (state) {
            const int descriptor = backtrace_open(data.fileName, errorHandler, &data, nullptr);
            if (descriptor >= 1) {
                int foundSym = 0;
                int foundDwarf = 0;
                auto ret = elf_add(state, descriptor, addressStart, errorHandler, &data, &state->fileline_fn, &foundSym,
                                   &foundDwarf, false);
                if (ret && foundSym) {
                    state->syminfo_fn = &elf_syminfo;
                }
            }
        }

        m_backtraceStates.insert(it, make_pair(originalFileName, state));

        return state;
    }

private:
    vector<Module> m_modules;
    unordered_map<std::string, backtrace_state*> m_backtraceStates;
    bool m_modulesDirty = false;

    unordered_map<string, size_t> m_internedData;
    unordered_map<uintptr_t, size_t> m_encounteredIps;
};
}

int main(int /*argc*/, char** /*argv*/)
{
    // optimize: we only have a single thread
    ios_base::sync_with_stdio(false);
    __fsetlocking(stdout, FSETLOCKING_BYCALLER);
    __fsetlocking(stdin, FSETLOCKING_BYCALLER);

    AccumulatedTraceData data;

    LineReader reader;

    string exe;

    PointerMap ptrToIndex;
    uint64_t lastPtr = 0;
    AllocationInfoSet allocationInfos;

    uint64_t allocations = 0;
    uint64_t leakedAllocations = 0;
    uint64_t temporaryAllocations = 0;

    while (reader.getLine(cin)) {
        if (reader.mode() == 'x') {
            reader >> exe;
        } else if (reader.mode() == 'm') {
            string fileName;
            reader >> fileName;
            if (fileName == "-") {
                data.clearModules();
            } else {
                if (fileName == "x") {
                    fileName = exe;
                }
                std::string internedString;
                const auto moduleIndex = data.intern(fileName, &internedString);
                uintptr_t addressStart = 0;
                if (!(reader >> addressStart)) {
                    cerr << "failed to parse line: " << reader.line() << endl;
                    return 1;
                }
                auto state = data.findBacktraceState(internedString, addressStart);
                uintptr_t vAddr = 0;
                uintptr_t memSize = 0;
                while ((reader >> vAddr) && (reader >> memSize)) {
                    data.addModule(state, moduleIndex, addressStart + vAddr, addressStart + vAddr + memSize);
                }
            }
        } else if (reader.mode() == 't') {
            uintptr_t instructionPointer = 0;
            size_t parentIndex = 0;
            if (!(reader >> instructionPointer) || !(reader >> parentIndex)) {
                cerr << "failed to parse line: " << reader.line() << endl;
                return 1;
            }
            // ensure ip is encountered
            const auto ipId = data.addIp(instructionPointer);
            // trace point, map current output index to parent index
            fprintf(stdout, "t %zx %zx\n", ipId, parentIndex);
        } else if (reader.mode() == '+') {
            ++allocations;
            ++leakedAllocations;
            uint64_t size = 0;
            TraceIndex traceId;
            uint64_t ptr = 0;
            if (!(reader >> size) || !(reader >> traceId.index) || !(reader >> ptr)) {
                cerr << "failed to parse line: " << reader.line() << endl;
                continue;
            }

            AllocationIndex index;
            if (allocationInfos.add(size, traceId, &index)) {
                fprintf(stdout, "a %" PRIx64 " %x\n", size, traceId.index);
            }
            ptrToIndex.addPointer(ptr, index);
            lastPtr = ptr;
            fprintf(stdout, "+ %x\n", index.index);
        } else if (reader.mode() == '-') {
            uint64_t ptr = 0;
            if (!(reader >> ptr)) {
                cerr << "failed to parse line: " << reader.line() << endl;
                continue;
            }
            bool temporary = lastPtr == ptr;
            lastPtr = 0;
            auto allocation = ptrToIndex.takePointer(ptr);
            if (!allocation.second) {
                continue;
            }
            fprintf(stdout, "- %x\n", allocation.first.index);
            if (temporary) {
                ++temporaryAllocations;
            }
            --leakedAllocations;
        } else {
            fputs(reader.line().c_str(), stdout);
            fputc('\n', stdout);
        }
    }

    fprintf(stderr, "heaptrack stats:\n"
                    "\tallocations:          \t%" PRIu64 "\n"
                    "\tleaked allocations:   \t%" PRIu64 "\n"
                    "\ttemporary allocations:\t%" PRIu64 "\n",
            allocations, leakedAllocations, temporaryAllocations);

    return 0;
}
