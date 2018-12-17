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
#include "util/linewriter.h"
#include "util/pointermap.h"

#include <signal.h>
#include <unistd.h>

using namespace std;

namespace {

#define error_out cerr << __FILE__ << ':' << __LINE__ << " ERROR:"

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

struct Frame
{
    Frame(string function = {}, string file = {}, int line = 0)
        : function(function)
        , file(file)
        , line(line)
    {
    }

    bool isValid() const
    {
        return !function.empty();
    }

    string function;
    string file;
    int line;
};

struct AddressInformation
{
    Frame frame;
    vector<Frame> inlined;
};

struct ResolvedFrame
{
    ResolvedFrame(size_t functionIndex = 0, size_t fileIndex = 0, int line = 0)
        : functionIndex(functionIndex)
        , fileIndex(fileIndex)
        , line(line)
    {
    }
    size_t functionIndex;
    size_t fileIndex;
    int line;
};

struct ResolvedIP
{
    size_t moduleIndex = 0;
    ResolvedFrame frame;
    vector<ResolvedFrame> inlined;
};

struct Module
{
    Module(string fileName, uintptr_t addressStart, uintptr_t addressEnd, backtrace_state* backtraceState,
           size_t moduleIndex)
        : fileName(fileName)
        , addressStart(addressStart)
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

        // try to find frame information from debug information
        backtrace_pcinfo(backtraceState, address,
                         [](void* data, uintptr_t /*addr*/, const char* file, int line, const char* function) -> int {
                             Frame frame(demangle(function), file ? file : "", line);
                             auto info = reinterpret_cast<AddressInformation*>(data);
                             if (!info->frame.isValid()) {
                                 info->frame = frame;
                             } else {
                                 info->inlined.push_back(frame);
                             }
                             return 0;
                         },
                         [](void* /*data*/, const char* /*msg*/, int /*errnum*/) {}, &info);

        // no debug information available? try to fallback on the symbol table information
        if (!info.frame.isValid()) {
            struct Data
            {
                AddressInformation* info;
                const Module* module;
                uintptr_t address;
            };
            Data data = {&info, this, address};
            backtrace_syminfo(
                backtraceState, address,
                [](void* data, uintptr_t /*pc*/, const char* symname, uintptr_t /*symval*/, uintptr_t /*symsize*/) {
                    if (symname) {
                        reinterpret_cast<Data*>(data)->info->frame.function = demangle(symname);
                    }
                },
                [](void* _data, const char* msg, int errnum) {
                    auto* data = reinterpret_cast<const Data*>(_data);
                    error_out << "Module backtrace error for address " << hex << data->address << dec << " in module "
                              << data->module->fileName << " (code " << errnum << "): " << msg << endl;
                },
                &data);
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

    string fileName;
    uintptr_t addressStart;
    uintptr_t addressEnd;
    size_t moduleIndex;
    backtrace_state* backtraceState;
};

struct AccumulatedTraceData
{
    AccumulatedTraceData()
        : out(fileno(stdout))
    {
        m_modules.reserve(256);
        m_backtraceStates.reserve(64);
        m_internedData.reserve(4096);
        m_encounteredIps.reserve(32768);
    }

    ~AccumulatedTraceData()
    {
        out.write("# strings: %zu\n# ips: %zu\n", m_internedData.size(), m_encounteredIps.size());
        out.flush();
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

        auto resolveFrame = [this](const Frame& frame) {
            return ResolvedFrame{intern(frame.function), intern(frame.file), frame.line};
        };

        ResolvedIP data;
        // find module for this instruction pointer
        auto module =
            lower_bound(m_modules.begin(), m_modules.end(), ip,
                        [](const Module& module, const uintptr_t ip) -> bool { return module.addressEnd < ip; });
        if (module != m_modules.end() && module->addressStart <= ip && module->addressEnd >= ip) {
            data.moduleIndex = module->moduleIndex;
            const auto info = module->resolveAddress(ip);
            data.frame = resolveFrame(info.frame);
            std::transform(info.inlined.begin(), info.inlined.end(), std::back_inserter(data.inlined), resolveFrame);
        }
        return data;
    }

    size_t intern(const string& str, const char** internedString = nullptr)
    {
        if (str.empty()) {
            return 0;
        }

        auto it = m_internedData.find(str);
        if (it != m_internedData.end()) {
            if (internedString) {
                *internedString = it->first.data();
            }
            return it->second;
        }
        const size_t id = m_internedData.size() + 1;
        it = m_internedData.insert(it, make_pair(str, id));
        if (internedString) {
            *internedString = it->first.data();
        }
        out.write("s ");
        out.write(str);
        out.write("\n");
        return id;
    }

    void addModule(string fileName, backtrace_state* backtraceState, const size_t moduleIndex,
                   const uintptr_t addressStart, const uintptr_t addressEnd)
    {
        m_modules.emplace_back(fileName, addressStart, addressEnd, backtraceState, moduleIndex);
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
        out.write("i %zx %zx", instructionPointer, ip.moduleIndex);
        if (ip.frame.functionIndex || ip.frame.fileIndex) {
            out.write(" %zx", ip.frame.functionIndex);
            if (ip.frame.fileIndex) {
                out.write(" %zx %x", ip.frame.fileIndex, ip.frame.line);
                for (const auto& inlined : ip.inlined) {
                    out.write(" %zx %zx %x", inlined.functionIndex, inlined.fileIndex, inlined.line);
                }
            }
        }
        out.write("\n");
        return ipId;
    }

    /**
     * Prevent the same file from being initialized multiple times.
     * This drastically cuts the memory consumption down
     */
    backtrace_state* findBacktraceState(const char* fileName, uintptr_t addressStart)
    {
        if (boost::algorithm::starts_with(fileName, "linux-vdso.so")) {
            // prevent warning, since this will always fail
            return nullptr;
        }

        auto it = m_backtraceStates.find(fileName);
        if (it != m_backtraceStates.end()) {
            return it->second;
        }

        struct CallbackData
        {
            const char* fileName;
        };
        CallbackData data = {fileName};

        auto errorHandler = [](void* rawData, const char* msg, int errnum) {
            auto data = reinterpret_cast<const CallbackData*>(rawData);
            error_out << "Failed to create backtrace state for module " << data->fileName << ": " << msg << " / "
                      << strerror(errnum) << " (error code " << errnum << ")" << endl;
        };

        auto state = backtrace_create_state(data.fileName, /* we are single threaded, so: not thread safe */ false,
                                            errorHandler, &data);

        if (state) {
            const int descriptor = backtrace_open(data.fileName, errorHandler, &data, nullptr);
            if (descriptor >= 1) {
                int foundSym = 0;
                int foundDwarf = 0;
                auto ret = elf_add(state, data.fileName, descriptor, addressStart, errorHandler, &data,
                                   &state->fileline_fn, &foundSym, &foundDwarf, false, false);
                if (ret && foundSym) {
                    state->syminfo_fn = &elf_syminfo;
                } else {
                    state->syminfo_fn = &elf_nosyms;
                }
            }
        }

        m_backtraceStates.insert(it, make_pair(fileName, state));

        return state;
    }

    LineWriter out;

private:
    vector<Module> m_modules;
    unordered_map<std::string, backtrace_state*> m_backtraceStates;
    bool m_modulesDirty = false;

    unordered_map<string, size_t> m_internedData;
    unordered_map<uintptr_t, size_t> m_encounteredIps;
};

struct Stats
{
    uint64_t allocations = 0;
    uint64_t leakedAllocations = 0;
    uint64_t temporaryAllocations = 0;
} c_stats;

void exitHandler()
{
    fflush(stdout);
    fprintf(stderr,
            "heaptrack stats:\n"
            "\tallocations:          \t%" PRIu64 "\n"
            "\tleaked allocations:   \t%" PRIu64 "\n"
            "\ttemporary allocations:\t%" PRIu64 "\n",
            c_stats.allocations, c_stats.leakedAllocations, c_stats.temporaryAllocations);
}
}

int main(int /*argc*/, char** /*argv*/)
{
    // optimize: we only have a single thread
    ios_base::sync_with_stdio(false);
    __fsetlocking(stdout, FSETLOCKING_BYCALLER);
    __fsetlocking(stdin, FSETLOCKING_BYCALLER);

    // output data at end, even when we get terminated
    std::atexit(exitHandler);

    AccumulatedTraceData data;

    LineReader reader;

    string exe;

    PointerMap ptrToIndex;
    uint64_t lastPtr = 0;
    AllocationInfoSet allocationInfos;

    while (reader.getLine(cin)) {
        if (reader.mode() == 'x') {
            if (!exe.empty()) {
                error_out << "received duplicate exe event - child process tracking is not yet supported" << endl;
                return 1;
            }
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
                const char* internedString = nullptr;
                const auto moduleIndex = data.intern(fileName, &internedString);
                uintptr_t addressStart = 0;
                if (!(reader >> addressStart)) {
                    error_out << "failed to parse line: " << reader.line() << endl;
                    return 1;
                }
                auto state = data.findBacktraceState(internedString, addressStart);
                uintptr_t vAddr = 0;
                uintptr_t memSize = 0;
                while ((reader >> vAddr) && (reader >> memSize)) {
                    data.addModule(fileName, state, moduleIndex, addressStart + vAddr, addressStart + vAddr + memSize);
                }
            }
        } else if (reader.mode() == 't') {
            uintptr_t instructionPointer = 0;
            size_t parentIndex = 0;
            if (!(reader >> instructionPointer) || !(reader >> parentIndex)) {
                error_out << "failed to parse line: " << reader.line() << endl;
                return 1;
            }
            // ensure ip is encountered
            const auto ipId = data.addIp(instructionPointer);
            // trace point, map current output index to parent index
            data.out.writeHexLine('t', ipId, parentIndex);
        } else if (reader.mode() == '+') {
            ++c_stats.allocations;
            ++c_stats.leakedAllocations;
            uint64_t size = 0;
            TraceIndex traceId;
            uint64_t ptr = 0;
            if (!(reader >> size) || !(reader >> traceId.index) || !(reader >> ptr)) {
                error_out << "failed to parse line: " << reader.line() << endl;
                continue;
            }

            AllocationInfoIndex index;
            if (allocationInfos.add(size, traceId, &index)) {
                data.out.writeHexLine('a', size, traceId.index);
            }
            ptrToIndex.addPointer(ptr, index);
            lastPtr = ptr;
            data.out.writeHexLine('+', index.index);
        } else if (reader.mode() == '-') {
            uint64_t ptr = 0;
            if (!(reader >> ptr)) {
                error_out << "failed to parse line: " << reader.line() << endl;
                continue;
            }
            bool temporary = lastPtr == ptr;
            lastPtr = 0;
            auto allocation = ptrToIndex.takePointer(ptr);
            if (!allocation.second) {
                continue;
            }
            data.out.writeHexLine('-', allocation.first.index);
            if (temporary) {
                ++c_stats.temporaryAllocations;
            }
            --c_stats.leakedAllocations;
        } else {
            data.out.write("%s\n", reader.line().c_str());
        }
    }

    return 0;
}
