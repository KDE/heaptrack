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
 * @file heaptrack_print.cpp
 *
 * @brief Evaluate and print the collected heaptrack data.
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

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/program_options.hpp>

using namespace std;
namespace po = boost::program_options;

namespace {

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

// sadly, C++ doesn't yet have opaque typedefs
template<typename Base>
struct Index
{
    size_t index = 0;

    explicit operator bool() const
    {
        return index;
    }

    bool operator<(Index o) const
    {
        return index < o.index;
    }

    bool operator!=(Index o) const
    {
        return index != o.index;
    }

    bool operator==(Index o) const
    {
        return index == o.index;
    }
};

template<typename Base>
istream& operator>>(istream &in, Index<Base> &index)
{
    in >> index.index;
    return in;
}

template<typename Base>
ostream& operator<<(ostream &out, const Index<Base> index)
{
    out << index.index;
    return out;
}

struct StringIndex : public Index<StringIndex> {};
struct ModuleIndex : public StringIndex {};
struct FunctionIndex : public StringIndex {};
struct FileIndex : public StringIndex {};
struct IpIndex : public Index<IpIndex> {};
struct TraceIndex : public Index<TraceIndex> {};

struct InstructionPointer
{
    uintptr_t instructionPointer = 0;
    ModuleIndex moduleIndex;
    FunctionIndex functionIndex;
    FileIndex fileIndex;
    int line = 0;
};

struct TraceNode
{
    IpIndex ipIndex;
    TraceIndex parentIndex;
};

struct AllocationData
{
    // number of allocations
    size_t allocations = 0;
    // bytes allocated in total
    size_t allocated = 0;
    // amount of bytes leaked
    size_t leaked = 0;
    // largest amount of bytes allocated
    size_t peak = 0;
};

struct Allocation : public AllocationData
{
    // backtrace entry point
    TraceIndex traceIndex;
};

/**
 * Merged allocation information by instruction pointer outside of alloc funcs
 */
struct MergedAllocation : public AllocationData
{
    // individual backtraces
    vector<Allocation> traces;
    // location
    IpIndex ipIndex;
};

/**
 * Information for a single call to an allocation function
 */
struct AllocationInfo
{
    TraceIndex traceIndex;
    size_t size;
};

struct AccumulatedTraceData
{
    AccumulatedTraceData()
    {
        instructionPointers.reserve(16384);
        traces.reserve(65536);
        strings.reserve(4096);
        allocations.reserve(16384);
        activeAllocations.reserve(65536);
    }

    void clear()
    {
        mainIndex.index = 0;
        instructionPointers.clear();
        traces.clear();
        strings.clear();
        mergedAllocations.clear();
        allocations.clear();
        activeAllocations.clear();
    }

    void printIp(const IpIndex ip, ostream &out, const size_t indent = 0) const
    {
        printIp(findIp(ip), out, indent);
    }

    void printIp(const InstructionPointer& ip, ostream& out, size_t indent = 0) const
    {
        while (indent--) {
            out << "  ";
        }
        out << "0x" << hex << ip.instructionPointer << dec;
        if (ip.moduleIndex) {
            out << ' ' << stringify(ip.moduleIndex);
        } else {
            out << " <unknown module>";
        }
        if (ip.functionIndex) {
            out << ' ' << prettyFunction(stringify(ip.functionIndex));
        } else {
            out << " ??";
        }
        if (ip.fileIndex) {
            out << ' ' << stringify(ip.fileIndex) << ':' << ip.line;
        }
        out << '\n';
    }

    void printBacktrace(const TraceIndex traceIndex, ostream& out,
                        const size_t indent = 0, bool skipFirst = false) const
    {
        printBacktrace(findTrace(traceIndex), out, indent, skipFirst);
    }

    void printBacktrace(TraceNode node, ostream& out, const size_t indent = 0,
                        bool skipFirst = false) const
    {
        while (node.ipIndex) {
            const auto& ip = findIp(node.ipIndex);
            if (!skipFirst) {
                printIp(ip, out, indent);
            }
            skipFirst = false;

            if (mainIndex && ip.functionIndex.index == mainIndex.index + 1) {
                break;
            }

            node = findTrace(node.parentIndex);
        };
    }

    template<typename T>
    void printMerged(T AllocationData::* member, const char* label)
    {
        sort(mergedAllocations.begin(), mergedAllocations.end(),
            [member] (const MergedAllocation& l, const MergedAllocation &r) {
                return l.*member > r.*member;
            });
        for (size_t i = 0; i < min(10lu, mergedAllocations.size()); ++i) {
            auto& allocation = mergedAllocations[i];
            cout << allocation.*member << ' ' << label << " from:\n";
            printIp(allocation.ipIndex, cout);

            sort(allocation.traces.begin(), allocation.traces.end(),
                [member] (const Allocation& l, const Allocation &r) {
                    return l.*member > r.*member;
                });
            size_t handled = 0;
            const size_t subTracesToPrint = 5;
            for (size_t j = 0; j < min(subTracesToPrint, allocation.traces.size()); ++j) {
                const auto& trace = allocation.traces[j];
                cout << "  " << trace.*member << " from:\n";
                handled += trace.*member;
                printBacktrace(trace.traceIndex, cout, 2, true);
            }
            if (allocation.traces.size() > subTracesToPrint) {
                cout << "  and " << (allocation.*member - handled) << " from "
                     << (allocation.traces.size() - subTracesToPrint) << " other places\n";
            }
            cout << '\n';
        }
    }

    const string& stringify(const StringIndex stringId) const
    {
        if (!stringId || stringId.index > strings.size()) {
            static const string empty;
            return empty;
        } else {
            return strings.at(stringId.index - 1);
        }
    }

    string prettyFunction(const string& function) const
    {
        if (!shortenTemplates) {
            return function;
        }
        string ret;
        ret.reserve(function.size());
        int depth = 0;
        for (size_t i = 0; i < function.size(); ++i) {
            const auto c = function[i];
            if (c == '<') {
                ++depth;
                if (depth == 1) {
                    ret.push_back(c);
                }
            } else if (c == '>') {
                --depth;
            }
            if (depth) {
                continue;
            }
            ret.push_back(c);
        }
        return ret;
    }

    bool read(istream& in)
    {
        clear();

        string line;
        line.reserve(1024);

        stringstream lineIn(ios_base::in);
        lineIn << hex;

        const string opNewStr("operator new(unsigned long)");
        StringIndex opNewStrIndex;
        IpIndex opNewIpIndex;
        const string opArrNewStr("operator new[](unsigned long)");
        StringIndex opArrNewStrIndex;
        IpIndex opArrNewIpIndex;

        while (in.good()) {
            getline(in, line);
            if (line.empty()) {
                continue;
            }
            const char mode = line[0];
            if (mode == '#') {
                continue;
            }
            lineIn.str(line);
            lineIn.clear();
            // skip mode and leading whitespace
            lineIn.seekg(2);
            if (mode == 's') {
                strings.push_back(line.substr(2));
                if (!opNewStrIndex && strings.back() == opNewStr) {
                    opNewStrIndex.index = strings.size();
                } else if (!opArrNewStrIndex && strings.back() == opArrNewStr) {
                    opArrNewStrIndex.index = strings.size();
                }
            } else if (mode == 't') {
                TraceNode node;
                lineIn >> node.ipIndex;
                lineIn >> node.parentIndex;
                traces.push_back(node);
            } else if (mode == 'i') {
                InstructionPointer ip;
                lineIn >> ip.instructionPointer;
                lineIn >> ip.moduleIndex;
                lineIn >> ip.functionIndex;
                lineIn >> ip.fileIndex;
                lineIn >> ip.line;
                instructionPointers.push_back(ip);
                if (opNewStrIndex && !opNewIpIndex && opNewStrIndex == ip.functionIndex) {
                    opNewIpIndex.index = instructionPointers.size();
                } else if (opArrNewStrIndex && !opArrNewIpIndex && opArrNewStrIndex == ip.functionIndex) {
                    opArrNewIpIndex.index = instructionPointers.size();
                }
            } else if (mode == '+') {
                size_t size = 0;
                lineIn >> size;
                TraceIndex traceId;
                lineIn >> traceId;
                uintptr_t ptr = 0;
                lineIn >> ptr;
                if (lineIn.bad()) {
                    cerr << "failed to parse line: " << line << endl;
                    return false;
                }

                activeAllocations[ptr] = {traceId, size};

                auto& allocation = findAllocation(traceId);
                allocation.leaked += size;
                allocation.allocated += size;
                ++allocation.allocations;
                if (allocation.leaked > allocation.peak) {
                    allocation.peak = allocation.leaked;
                }
                totalAllocated += size;
                ++totalAllocations;
                leaked += size;
                if (leaked > peak) {
                    peak = leaked;
                }
                ++sizeHistogram[size];
            } else if (mode == '-') {
                uintptr_t ptr = 0;
                lineIn >> ptr;
                if (lineIn.bad()) {
                    cerr << "failed to parse line: " << line << endl;
                    return false;
                }
                auto ip = activeAllocations.find(ptr);
                if (ip == activeAllocations.end()) {
                    cerr << "unknown pointer in line: " << line << endl;
                    continue;
                }
                const auto info = ip->second;
                activeAllocations.erase(ip);

                auto& allocation = findAllocation(info.traceIndex);
                if (!allocation.allocations || allocation.leaked < info.size) {
                    cerr << "inconsistent allocation info, underflowed allocations of " << info.traceIndex << endl;
                    allocation.leaked = 0;
                    allocation.allocations = 0;
                } else {
                    allocation.leaked -= info.size;
                }
                leaked -= info.size;
            } else {
                cerr << "failed to parse line: " << line << endl;
            }
        }

        /// these are leaks, but we now have the same data in \c allocations as well
        activeAllocations.clear();

        // find index of "main" index which can be used to terminate backtraces
        // and prevent printing stuff above main, which is usually uninteresting
        mainIndex.index = findStringIndex("main");

        // skip operator new and operator new[] at the beginning of traces
        if (opNewIpIndex || opArrNewIpIndex) {
            for (Allocation& allocation : allocations) {
                while (true) {
                    auto trace = findTrace(allocation.traceIndex);
                    if (trace.ipIndex == opNewIpIndex || trace.ipIndex == opArrNewIpIndex) {
                        allocation.traceIndex = trace.parentIndex;
                    } else {
                        break;
                    }
                }
            }
        }

        // merge allocations so that different traces that point to the same
        // instruction pointer at the end where the allocation function is
        // called are combined
        // TODO: merge deeper traces, i.e. A,B,C,D and A,B,C,F
        //       should be merged to A,B,C: D & F
        //       currently the below will only merge it to: A: B,C,D & B,C,F
        mergedAllocations.reserve(allocations.size());
        for (const Allocation& allocation : allocations) {
            mergeAllocation(allocation);
        }
        for (MergedAllocation& merged : mergedAllocations) {
            for (const Allocation& allocation: merged.traces) {
                merged.allocated += allocation.allocated;
                merged.allocations += allocation.allocations;
                merged.leaked += allocation.leaked;
                merged.peak += allocation.peak;
            }
        }

        return true;
    }

    bool shortenTemplates = false;

    vector<Allocation> allocations;
    vector<MergedAllocation> mergedAllocations;
    map<size_t, size_t> sizeHistogram;
    size_t totalAllocated = 0;
    size_t totalAllocations = 0;
    size_t peak = 0;
    size_t leaked = 0;

private:
    Allocation& findAllocation(const TraceIndex traceIndex)
    {
        auto it = lower_bound(allocations.begin(), allocations.end(), traceIndex,
                                [] (const Allocation& allocation, const TraceIndex traceIndex) -> bool {
                                    return allocation.traceIndex < traceIndex;
                                });
        if (it == allocations.end() || it->traceIndex != traceIndex) {
            Allocation allocation;
            allocation.traceIndex = traceIndex;
            it = allocations.insert(it, allocation);
        }
        return *it;
    }

    void mergeAllocation(const Allocation& allocation)
    {
        const auto trace = findTrace(allocation.traceIndex);
        auto it = lower_bound(mergedAllocations.begin(), mergedAllocations.end(), trace,
                                [] (const MergedAllocation& allocation, const TraceNode trace) -> bool {
                                    return allocation.ipIndex < trace.ipIndex;
                                });
        if (it == mergedAllocations.end() || it->ipIndex != trace.ipIndex) {
            MergedAllocation merged;
            merged.ipIndex = trace.ipIndex;
            it = mergedAllocations.insert(it, merged);
        }
        it->traces.push_back(allocation);
    }

    size_t findStringIndex(const char* const str) const
    {
        auto it = find(strings.begin(), strings.end(), str);
        if (it != strings.end()) {
            return distance(strings.begin(), it);
        } else {
            return 0;
        }
    }

    InstructionPointer findIp(const IpIndex ipIndex) const
    {
        if (!ipIndex || ipIndex.index > instructionPointers.size()) {
            return {};
        } else {
            return instructionPointers[ipIndex.index - 1];
        }
    }

    TraceNode findTrace(const TraceIndex traceIndex) const
    {
        if (!traceIndex || traceIndex.index > traces.size()) {
            return {};
        } else {
            return traces[traceIndex.index - 1];
        }
    }

    StringIndex mainIndex;
    unordered_map<uintptr_t, AllocationInfo> activeAllocations;
    vector<InstructionPointer> instructionPointers;
    vector<TraceNode> traces;
    vector<string> strings;
};

}

int main(int argc, char** argv)
{
    po::options_description desc("Options");
    desc.add_options()
        ("file,f", po::value<string>()->required(),
            "The heaptrack data file to print.")
        ("shorten-templates,t", po::value<bool>()->default_value(true)->implicit_value(true),
            "Shorten template identifiers.")
        ("print-peaks,p", po::value<bool>()->default_value(true)->implicit_value(true),
            "Print backtraces to top allocators, sorted by peak consumption.")
        ("print-allocators,a", po::value<bool>()->default_value(true)->implicit_value(true),
            "Print backtraces to top allocators, sorted by number of calls to allocation functions.")
        ("print-leaks,l", po::value<bool>()->default_value(false)->implicit_value(true),
            "Print backtraces to leaked memory allocations.")
        ("print-histogram,H", po::value<bool>()->default_value(false)->implicit_value(true),
            "Print allocation size histogram.")
        ("print-overall-allocated,o", po::value<bool>()->default_value(false)->implicit_value(true),
            "Print top overall allocators, ignoring memory frees.")
        ("help,h",
            "Show this help message.");
    po::positional_options_description p;
    p.add("file", -1);

    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv)
                    .options(desc).positional(p).run(), vm);
        if (vm.count("help")) {
            cout << "heaptrack_print - analyze heaptrack data files.\n"
                << "\n"
                << "heaptrack is a heap memory profiler which records information\n"
                << "about calls to heap allocation functions such as malloc, operator new etc. pp.\n"
                << "This print utility can then be used to analyze the generated data files.\n\n"
                << desc << endl;
            return 1;
        }
        po::notify(vm);
    } catch (const po::error& error) {
        cerr << "ERROR: " << error.what() << endl
             << endl << desc << endl;
        return 1;
    }

    AccumulatedTraceData data;

    // optimize: we only have a single thread
    ios_base::sync_with_stdio(false);

    const auto inputFile = vm["file"].as<string>();
    data.shortenTemplates = vm["shorten-templates"].as<bool>();
    const bool printHistogram = vm["print-histogram"].as<bool>();
    const bool printLeaks = vm["print-leaks"].as<bool>();
    const bool printOverallAlloc = vm["print-overall-allocated"].as<bool>();
    const bool printPeaks = vm["print-peaks"].as<bool>();
    const bool printAllocs = vm["print-allocators"].as<bool>();

    string fileName(inputFile);
    const bool isCompressed = boost::algorithm::ends_with(fileName, ".gz");
    ifstream file(fileName, isCompressed ? ios_base::in | ios_base::binary : ios_base::in);

    if (!file.is_open()) {
        cerr << "Failed to open heaptrack log file: " << inputFile << endl
             << endl << desc << endl;
        return 1;
    }

    boost::iostreams::filtering_istream in;
    if (isCompressed) {
        in.push(boost::iostreams::gzip_decompressor());
    }
    in.push(file);

    cout << "reading file \"" << fileName << "\" - please wait, this might take some time..." << endl;

    if (!data.read(in)) {
        return 1;
    }

    cout << "finished reading file, now analyzing data:\n" << endl;

    if (printAllocs) {
        // sort by amount of allocations
        cout << "MOST CALLS TO ALLOCATION FUNCTIONS\n";
        data.printMerged(&AllocationData::allocations, "calls to allocation functions");
        cout << endl;
    }

    if (printOverallAlloc) {
        cout << "MOST BYTES ALLOCATED OVER TIME (ignoring deallocations)\n";
        data.printMerged(&AllocationData::allocated, "bytes allocated");
        cout << endl;
    }

    if (printPeaks) {
        ///FIXME: find a way to merge this without breaking temporal dependency.
        /// I.e. a given function could be called N times from different places
        /// and allocate M bytes each, but free it thereafter.
        /// Then the below would give a wrong total peak size of N * M instead
        /// of just N!

        // sort by peak memory consumption
        sort(data.allocations.begin(), data.allocations.end(),
            [] (const Allocation& l, const Allocation &r) {
                return l.peak > r.peak;
            });
        cout << "PEAK MEMORY CONSUMERS\n";
        for (size_t i = 0; i < min(10lu, data.allocations.size()); ++i) {
            const auto& allocation = data.allocations[i];
            cout << allocation.peak << " bytes allocated at peak:\n";
            data.printBacktrace(allocation.traceIndex, cout);
            cout << '\n';
        }
        cout << endl;
    }

    if (printLeaks) {
        // sort by amount of leaks
        cout << "MEMORY LEAKS\n";
        data.printMerged(&AllocationData::leaked, "bytes leaked");
        cout << endl;
    }

    cout << data.totalAllocated << " bytes allocated in total (ignoring deallocations) over "
         << data.totalAllocations << " calls to allocation functions.\n"
         << "peak heap memory consumption: " << data.peak << " bytes\n"
         << "total memory leaked: " << data.leaked << " bytes\n";

    if (printHistogram) {
        cout << "size histogram: " << endl;
        for (auto entry : data.sizeHistogram) {
            cout << entry.first << "\t" << entry.second << endl;
        }
    }

    return 0;
}
