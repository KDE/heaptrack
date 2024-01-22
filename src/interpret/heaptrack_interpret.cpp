/*
    SPDX-FileCopyrightText: 2014-2022 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: GPL-2.0-or-later
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
#ifdef __linux__
#include <stdio_ext.h>
#endif
#include <memory>
#include <tuple>
#include <vector>

#include "dwarfdiecache.h"
#include "symbolcache.h"

#include "util/linereader.h"
#include "util/linewriter.h"
#include "util/pointermap.h"

#include <boost/program_options.hpp>

#include <csignal>
#include <dwarf.h>
#include <elfutils/libdwelf.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;
namespace po = boost::program_options;

namespace {
bool isArmArch()
{
#ifdef __arm__
    return true;
#else
    return false;
#endif
}

#define error_out cerr << __FILE__ << ':' << __LINE__ << " ERROR:"

bool startsWith(const std::string& haystack, const char* needle)
{
    return haystack.compare(0, strlen(needle), needle) == 0;
}

static uint64_t alignedAddress(uint64_t addr, bool isArmArch)
{
    // Adjust addr back. The symtab entries are 1 off for all practical purposes.
    return (isArmArch && (addr & 1)) ? addr - 1 : addr;
}

static SymbolCache::Symbols extractSymbols(Dwfl_Module* module, uint64_t elfStart, bool isArmArch)
{
    SymbolCache::Symbols symbols;

    const auto numSymbols = dwfl_module_getsymtab(module);
    if (numSymbols <= 0)
        return symbols;

    symbols.reserve(numSymbols);
    for (int i = 0; i < numSymbols; ++i) {
        GElf_Sym sym;
        GElf_Addr symAddr;
        const auto symbol = dwfl_module_getsym_info(module, i, &sym, &symAddr, nullptr, nullptr, nullptr);
        if (symbol) {
            const uint64_t start = alignedAddress(sym.st_value, isArmArch);
            symbols.push_back({symAddr - elfStart, start, sym.st_size, symbol});
        }
    }
    return symbols;
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

struct ModuleFragment
{
    ModuleFragment(string fileName, uintptr_t addressStart, uintptr_t fragmentStart, uintptr_t fragmentEnd,
                   size_t moduleIndex)
        : fileName(fileName)
        , addressStart(addressStart)
        , fragmentStart(fragmentStart)
        , fragmentEnd(fragmentEnd)
        , moduleIndex(moduleIndex)
    {
    }

    bool operator<(const ModuleFragment& module) const
    {
        return tie(addressStart, fragmentStart, fragmentEnd, moduleIndex)
            < tie(module.addressStart, module.fragmentStart, module.fragmentEnd, module.moduleIndex);
    }

    bool operator!=(const ModuleFragment& module) const
    {
        return tie(addressStart, fragmentStart, fragmentEnd, moduleIndex)
            != tie(module.addressStart, module.fragmentStart, module.fragmentEnd, module.moduleIndex);
    }

    string fileName;
    uintptr_t addressStart;
    uintptr_t fragmentStart;
    uintptr_t fragmentEnd;
    size_t moduleIndex;
};

struct Module
{
    Module(string fileName, uintptr_t addressStart, Dwfl_Module* module, SymbolCache* symbolCache)
        : fileName(std::move(fileName))
        , addressStart(addressStart)
        , module(module)
        , dieCache(module)
        , symbolCache(symbolCache)
    {
    }

    Module()
        : Module({}, 0, nullptr, nullptr)
    {
    }

    AddressInformation resolveAddress(uintptr_t address) const
    {
        AddressInformation info;

        if (!module) {
            return info;
        }

        if (!symbolCache->hasSymbols(fileName)) {
            // cache all symbols in a sorted lookup table and demangle them on-demand
            // note that the symbols within the symtab aren't necessarily sorted,
            // which makes searching repeatedly via dwfl_module_addrinfo potentially very slow
            symbolCache->setSymbols(fileName, extractSymbols(module, addressStart, isArmArch()));
        }

        auto cachedAddrInfo = symbolCache->findSymbol(fileName, address - addressStart);
        if (cachedAddrInfo.isValid()) {
            info.frame.function = std::move(cachedAddrInfo.symname);
        }

        auto cuDie = dieCache.findCuDie(address);
        if (!cuDie) {
            return info;
        }

        const auto offset = address - cuDie->bias();
        auto srcloc = dwarf_getsrc_die(cuDie->cudie(), offset);
        if (srcloc) {
            const char* srcfile = dwarf_linesrc(srcloc, nullptr, nullptr);
            if (srcfile) {
                const auto file = std::string(srcfile);
                info.frame.file = srcfile;
                dwarf_lineno(srcloc, &info.frame.line);
            }
        }

        auto* subprogram = cuDie->findSubprogramDie(offset);
        if (!subprogram) {
            return info;
        }

        // resolve the inline chain if possible
        auto scopes = findInlineScopes(subprogram->die(), offset);

        if (scopes.empty()) {
            // no inline frames, use subprogram name directly and return
            info.frame.function = cuDie->dieName(subprogram->die());
            return info;
        }

        // use name of the last inlined function as symbol
        info.frame.function = cuDie->dieName(&scopes.back());

        Dwarf_Files* files = nullptr;
        dwarf_getsrcfiles(cuDie->cudie(), &files, nullptr);

        auto handleDie = [&](Dwarf_Die *scope, Dwarf_Die *prevScope) {
            const auto tag = dwarf_tag(prevScope);
            if (tag != DW_TAG_inlined_subroutine) {
                error_out << "unexpected prev scope tag: " << std::hex << tag << '\n';
                return;
            }

            auto call = callSourceLocation(prevScope, files, cuDie->cudie());
            info.inlined.push_back({cuDie->dieName(scope), std::move(call.file), call.line});
        };

        // iterate in reverse, to properly rebuild the inline stack
        // note that we need to take the DW_AT_call_{file,line} from the previous scope DIE
        const auto numScopes = scopes.size();
        for (std::size_t scopeIndex = numScopes - 1; scopeIndex >= 1; --scopeIndex) {
            handleDie(&scopes[scopeIndex - 1], &scopes[scopeIndex]);
        }

        // the very last frame is the one where all the code got inlined into
        handleDie(subprogram->die(), &scopes.front());

        return info;
    }

    string fileName;
    uintptr_t addressStart;
    Dwfl_Module* module;
    mutable DwarfDieCache dieCache;
    SymbolCache* symbolCache;
};

struct AccumulatedTraceData
{
    AccumulatedTraceData(const std::string& sysroot, const std::vector<std::string>& debugPaths,
                         const std::vector<std::string>& extraPaths)
        : out(fileno(stdout))
        , m_sysroot(sysroot)
        , m_debugPaths(debugPaths)
        , m_extraPaths(extraPaths)
    {
        initializePaths();
        m_moduleFragments.reserve(256);
        m_internedData.reserve(4096);
        m_encounteredIps.reserve(32768);

        m_callbacks = {
            &dwfl_build_id_find_elf,
            &dwfl_standard_find_debuginfo,
            &dwfl_offline_section_address,
            &m_debugPath,
        };

        m_dwfl = dwfl_begin(&m_callbacks);
    }

    ~AccumulatedTraceData()
    {
        out.write("# strings: %zu\n# ips: %zu\n", m_internedData.size(), m_encounteredIps.size());
        out.flush();

        delete[] m_debugPath;
        dwfl_end(m_dwfl);
    }

    ResolvedIP resolve(const uintptr_t ip)
    {
        if (m_modulesDirty) {
            // sort by addresses, required for binary search below
            sort(m_moduleFragments.begin(), m_moduleFragments.end());

#ifndef NDEBUG
            for (size_t i = 0; i < m_moduleFragments.size(); ++i) {
                const auto& m1 = m_moduleFragments[i];
                for (size_t j = i + 1; j < m_moduleFragments.size(); ++j) {
                    if (i == j) {
                        continue;
                    }
                    const auto& m2 = m_moduleFragments[j];
                    if ((m1.fragmentStart <= m2.fragmentStart && m1.fragmentEnd > m2.fragmentStart)
                        || (m1.fragmentStart < m2.fragmentEnd && m1.fragmentEnd >= m2.fragmentEnd)) {
                        cerr << "OVERLAPPING MODULES: " << hex << m1.moduleIndex << " (" << m1.fragmentStart << " to "
                             << m1.fragmentEnd << ") and " << m1.moduleIndex << " (" << m2.fragmentStart << " to "
                             << m2.fragmentEnd << ")\n"
                             << dec;
                    } else if (m2.fragmentStart >= m1.fragmentEnd) {
                        break;
                    }
                }
            }
#endif

            // reset dwfl state
            m_modules.clear();

            dwfl_report_begin(m_dwfl);
            dwfl_report_end(m_dwfl, nullptr, nullptr);

            m_modulesDirty = false;
        }

        auto resolveFrame = [this](const Frame& frame) {
            return ResolvedFrame {intern(frame.function), intern(frame.file), frame.line};
        };

        ResolvedIP data;
        // find module for this instruction pointer
        auto fragment = lower_bound(
            m_moduleFragments.begin(), m_moduleFragments.end(), ip,
            [](const ModuleFragment& fragment, const uintptr_t ip) -> bool { return fragment.fragmentEnd < ip; });
        if (fragment != m_moduleFragments.end() && fragment->fragmentStart <= ip && fragment->fragmentEnd >= ip) {
            data.moduleIndex = fragment->moduleIndex;

            if (auto module = reportModule(*fragment)) {
                const auto info = module->resolveAddress(ip);
                data.frame = resolveFrame(info.frame);
                std::transform(info.inlined.begin(), info.inlined.end(), std::back_inserter(data.inlined),
                               resolveFrame);
            }
        }
        return data;
    }

    size_t intern(const string& str, const char** internedString = nullptr)
    {
        if (str.empty()) {
            return 0;
        }

        const size_t id = m_internedData.size() + 1;
        auto inserted = m_internedData.insert({str, id});
        if (internedString) {
            *internedString = inserted.first->first.data();
        }

        if (!inserted.second) {
            return inserted.first->second;
        }

        out.write("s ");
        out.write(str);
        out.write("\n");
        return id;
    }

    void addModule(string fileName, const size_t moduleIndex, const uintptr_t addressStart,
                   const uintptr_t fragmentStart, const uintptr_t fragmentEnd)
    {
        if (!m_sysroot.empty()) {
            fileName = m_sysroot + fileName;
        }
        m_moduleFragments.emplace_back(fileName, addressStart, fragmentStart, fragmentEnd, moduleIndex);
        m_modulesDirty = true;
    }

    void clearModules()
    {
        // TODO: optimize this, reuse modules that are still valid
        m_moduleFragments.clear();
        m_modulesDirty = true;
    }

    size_t addIp(const uintptr_t instructionPointer)
    {
        if (!instructionPointer) {
            return 0;
        }

        const size_t ipId = m_encounteredIps.size() + 1;
        auto inserted = m_encounteredIps.insert({instructionPointer, ipId});
        if (!inserted.second) {
            return inserted.first->second;
        }

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

    LineWriter out;

private:
    Module* reportModule(const ModuleFragment& module)
    {
        if (startsWith(module.fileName, "linux-vdso.so")) {
            return nullptr;
        }

        auto& ret = m_modules[module.fileName];
        if (ret.module)
            return &ret;

        auto dwflModule = dwfl_addrmodule(m_dwfl, module.addressStart);
        if (!dwflModule) {
            dwfl_report_begin_add(m_dwfl);
            dwflModule = dwfl_report_elf(m_dwfl, module.fileName.c_str(), module.fileName.c_str(), -1,
                                         module.addressStart, false);
            dwfl_report_end(m_dwfl, nullptr, nullptr);

            if (!dwflModule) {
                error_out << "Failed to report module for " << module.fileName << ": " << dwfl_errmsg(dwfl_errno())
                          << endl;
                return nullptr;
            }
        }

        ret = Module(module.fileName, module.addressStart, dwflModule, &m_symbolCache);
        return &ret;
    }

    void initializePaths()
    {
        std::string path;
        for (const auto& p : m_debugPaths) {
            path += p + ":";
        }
        for (const auto& p : m_extraPaths) {
            path += p + ":";
        }
        path += ".debug:" + m_sysroot + "/usr/lib/debug";

        // Allocate memory and copy the string
        m_debugPath = new char[path.size() + 1];
        std::strcpy(m_debugPath, path.c_str());
    }

    vector<ModuleFragment> m_moduleFragments;
    Dwfl* m_dwfl = nullptr;
    char* m_debugPath = nullptr;
    Dwfl_Callbacks m_callbacks;
    SymbolCache m_symbolCache;
    bool m_modulesDirty = false;

    std::string m_sysroot;
    std::vector<std::string> m_debugPaths;
    std::vector<std::string> m_extraPaths;

    tsl::robin_map<string, size_t> m_internedData;
    tsl::robin_map<uintptr_t, size_t> m_encounteredIps;
    tsl::robin_map<string, Module> m_modules;
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

int main(int argc, char** argv)
{
    po::options_description desc("Options", 120, 60);

    // clang-format off
    desc.add_options()
        ("sysroot", po::value<std::string>(),
            "Path to a system root directory")
        ("debug-paths", po::value<std::vector<std::string>>()->multitoken(),
            "Paths to folders containing extra debug symbols\nSee e.g. https://sourceware.org/gdb/current/onlinedocs/gdb.html/Separate-Debug-Files.html")
        ("extra-paths", po::value<std::vector<std::string>>()->multitoken(),
            "Paths to folders containing additional executables or libraries with debug symbols, e.g. for side loading");
    // clang-format on

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch (const po::error& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        std::cerr << desc << std::endl;
        return 1;
    }

    std::string sysroot;
    std::vector<std::string> debugPaths;
    std::vector<std::string> extraPaths;

    if (vm.count("sysroot")) {
        sysroot = vm["sysroot"].as<std::string>();
    }
    if (vm.count("debug-paths")) {
        debugPaths = vm["debug-paths"].as<std::vector<std::string>>();
    }
    if (vm.count("extra-paths")) {
        extraPaths = vm["extra-paths"].as<std::vector<std::string>>();
    }

    [] {
        // NOTE: we disable debuginfod by default as it can otherwise lead to
        //       nasty delays otherwise which are highly unexpected to users
        //       if desired, they can opt in to that via
        //
        //       export HEAPTRACK_ENABLE_DEBUGINFOD=1
        if (!getenv("DEBUGINFOD_URLS")) {
            return;
        }

        auto enable = getenv("HEAPTRACK_ENABLE_DEBUGINFOD");
        if (!enable || !atoi(enable)) {
            fprintf(stderr,
                    "NOTE: heaptrack detected DEBUGINFOD_URLS but will disable it to prevent \n"
                    "unintended network delays during recording\n"
                    "If you really want to use DEBUGINFOD, export HEAPTRACK_ENABLE_DEBUGINFOD=1\n");
            unsetenv("DEBUGINFOD_URLS");
        }
    }();

    // optimize: we only have a single thread
    ios_base::sync_with_stdio(false);
#ifdef __linux__
    __fsetlocking(stdout, FSETLOCKING_BYCALLER);
    __fsetlocking(stdin, FSETLOCKING_BYCALLER);
#endif

    // output data at end, even when we get terminated
    std::atexit(exitHandler);

    AccumulatedTraceData data(sysroot, debugPaths, extraPaths);

    LineReader reader;

    string exe;

    PointerMap ptrToIndex;
    uint64_t lastPtr = 0;
    AllocationInfoSet allocationInfos;

    while (reader.getLine(cin)) {
        if (reader.mode() == 'v') {
            unsigned int heaptrackVersion = 0;
            reader >> heaptrackVersion;
            unsigned int fileVersion = 0;
            reader >> fileVersion;
            if (fileVersion >= 3) {
                reader.setExpectedSizedStrings(true);
            }
            data.out.write("%s\n", reader.line().c_str());
        } else if (reader.mode() == 'x') {
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
                uintptr_t vAddr = 0;
                uintptr_t memSize = 0;
                while ((reader >> vAddr) && (reader >> memSize)) {
                    data.addModule(fileName, moduleIndex, addressStart, addressStart + vAddr,
                                   addressStart + vAddr + memSize);
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
