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
#include <unordered_set>
#include <map>
#include <vector>
#include <memory>
#include <tuple>
#include <algorithm>
#include <cassert>
#include <iomanip>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/program_options.hpp>

#include "linereader.h"

using namespace std;
namespace po = boost::program_options;

namespace {

class formatBytes
{
public:
    formatBytes(size_t bytes)
        : m_bytes(bytes)
    {
    }

    friend ostream& operator<<(ostream& out, const formatBytes data)
    {
        if (data.m_bytes < 1000) {
            // no fancy formatting for plain byte values, esp. no .00 factions
            return out << data.m_bytes << 'B';
        }

        static const auto units = {
            "B",
            "KB",
            "MB",
            "GB",
            "TB"
        };
        auto unit = units.begin();
        size_t i = 0;
        double bytes = data.m_bytes;
        while (i < units.size() - 1 && bytes > 1000.) {
            bytes /= 1000.;
            ++i;
            ++unit;
        }
        return out << fixed << setprecision(2) << bytes << *unit;
    }

private:
    size_t m_bytes;
};

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
bool operator>>(LineReader& reader, Index<Base> &index)
{
    return reader.readHex(index.index);
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

    bool compareWithoutAddress(const InstructionPointer &other) const
    {
        return make_tuple(moduleIndex, functionIndex, fileIndex, line)
             < make_tuple(other.moduleIndex, other.functionIndex, other.fileIndex, other.line);
    }

    bool equalWithoutAddress(const InstructionPointer &other) const
    {
        return make_tuple(moduleIndex, functionIndex, fileIndex, line)
            == make_tuple(other.moduleIndex, other.functionIndex, other.fileIndex, other.line);
    }
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
        opNewIpIndices.reserve(16);
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
        opNewIpIndices.clear();
    }

    void printIp(const IpIndex ip, ostream &out, const size_t indent = 0) const
    {
        printIp(findIp(ip), out, indent);
    }

    void printIndent(ostream& out, size_t indent, const char* indentString = "  ") const
    {
        while (indent--) {
            out << indentString;
        }
    }

    void printIp(const InstructionPointer& ip, ostream& out, const size_t indent = 0) const
    {
        printIndent(out, indent);

        if (ip.functionIndex) {
            out << prettyFunction(stringify(ip.functionIndex));
        } else {
            out << "0x" << hex << ip.instructionPointer << dec;
        }

        out << '\n';
        printIndent(out, indent + 1);

        if (ip.fileIndex) {
            out << "at " << stringify(ip.fileIndex) << ':' << ip.line << '\n';
            printIndent(out, indent + 1);
        }

        if (ip.moduleIndex) {
            out << "in " << stringify(ip.moduleIndex);
        } else {
            out << "in ??";
        }
        out << '\n';
    }

    void printBacktrace(const TraceIndex traceIndex, ostream& out,
                        const size_t indent = 0, bool skipFirst = false) const
    {
        if (!traceIndex) {
            out << "  ??";
            return;
        }
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

            if (mainIndex && ip.functionIndex.index == mainIndex.index) {
                break;
            }

            node = findTrace(node.parentIndex);
        };
    }

    template<typename T, typename LabelPrinter, typename SubLabelPrinter>
    void printAllocations(T AllocationData::* member, LabelPrinter label, SubLabelPrinter sublabel)
    {
        if (mergeBacktraces) {
            printMerged(member, label, sublabel);
        } else {
            printUnmerged(member, label);
        }
    }

    template<typename T, typename LabelPrinter, typename SubLabelPrinter>
    void printMerged(T AllocationData::* member, LabelPrinter label, SubLabelPrinter sublabel)
    {
        auto sortOrder = [member] (const AllocationData& l, const AllocationData& r) {
            return l.*member > r.*member;
        };
        sort(mergedAllocations.begin(), mergedAllocations.end(), sortOrder);
        for (size_t i = 0; i < min(10lu, mergedAllocations.size()); ++i) {
            auto& allocation = mergedAllocations[i];
            if (!(allocation.*member)) {
                break;
            }
            label(allocation);
            printIp(allocation.ipIndex, cout);

            sort(allocation.traces.begin(), allocation.traces.end(), sortOrder);
            size_t handled = 0;
            const size_t subTracesToPrint = 5;
            for (size_t j = 0; j < min(subTracesToPrint, allocation.traces.size()); ++j) {
                const auto& trace = allocation.traces[j];
                sublabel(trace);
                handled += trace.*member;
                printBacktrace(trace.traceIndex, cout, 2, true);
            }
            if (allocation.traces.size() > subTracesToPrint) {
                cout << "  and ";
                if (member == &AllocationData::allocations) {
                    cout << (allocation.*member - handled);
                } else {
                    cout << formatBytes(allocation.*member - handled);
                }
                cout << " from " << (allocation.traces.size() - subTracesToPrint) << " other places\n";
            }
            cout << '\n';
        }
    }

    template<typename T, typename LabelPrinter>
    void printUnmerged(T AllocationData::* member, LabelPrinter label)
    {
        sort(allocations.begin(), allocations.end(),
            [member] (const Allocation& l, const Allocation &r) {
                return l.*member > r.*member;
            });
        for (size_t i = 0; i < min(10lu, allocations.size()); ++i) {
            const auto& allocation = allocations[i];
            if (!(allocation.*member)) {
                break;
            }
            label(allocation);
            printBacktrace(allocation.traceIndex, cout, 1);
            cout << '\n';
        }
        cout << endl;
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
            if ((c == '<' || c == '>') && ret.size() >= 8) {
                // don't get confused by C++ operators
                const char* cmp = "operator";
                if (ret.back() == c) {
                    // skip second angle bracket for operator<< or operator>>
                    if (c == '<') {
                        cmp = "operator<";
                    } else {
                        cmp = "operator>";
                    }
                }
                if (boost::algorithm::ends_with(ret, cmp)) {
                    ret.push_back(c);
                    continue;
                }
            }
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

        LineReader reader;
        size_t timeStamp = 0;

        const string opNewStr("operator new(unsigned long)");
        const string opArrNewStr("operator new[](unsigned long)");
        const string mainStr("main");
        const string libcMainStr("__libc_start_main");
        bool mainStrIsLibC = false;
        StringIndex opNewStrIndex;
        StringIndex opArrNewStrIndex;

        while (reader.getLine(in)) {
            if (reader.mode() == 's') {
                strings.push_back(reader.line().substr(2));
                if (!opNewStrIndex && strings.back() == opNewStr) {
                    opNewStrIndex.index = strings.size();
                } else if (!opArrNewStrIndex && strings.back() == opArrNewStr) {
                    opArrNewStrIndex.index = strings.size();
                } else if ((mainStrIsLibC || !mainIndex) && strings.back() == mainStr) {
                    mainIndex.index = strings.size();
                    mainStrIsLibC = false;
                } else if (!mainIndex && strings.back() == libcMainStr) {
                    mainIndex.index = strings.size();
                    mainStrIsLibC = true;
                }
            } else if (reader.mode() == 't') {
                TraceNode node;
                reader >> node.ipIndex;
                reader >> node.parentIndex;
                // skip operator new and operator new[] at the beginning of traces
                if (!opNewIpIndices.empty()) {
                    while (true) {
                        if (opNewIpIndices.find(node.ipIndex.index) != opNewIpIndices.end()) {
                            node = findTrace(node.parentIndex);
                        } else {
                            break;
                        }
                    }
                }
                traces.push_back(node);
            } else if (reader.mode() == 'i') {
                InstructionPointer ip;
                reader >> ip.instructionPointer;
                reader >> ip.moduleIndex;
                reader >> ip.functionIndex;
                reader >> ip.fileIndex;
                reader >> ip.line;
                instructionPointers.push_back(ip);
                if ((opNewStrIndex && opNewStrIndex == ip.functionIndex)
                    || (opArrNewStrIndex && opArrNewStrIndex == ip.functionIndex))
                {
                    opNewIpIndices.insert(instructionPointers.size());
                }
            } else if (reader.mode() == '+') {
                size_t size = 0;
                TraceIndex traceId;
                uintptr_t ptr = 0;
                if (!(reader >> size) || !(reader >> traceId) || !(reader >> ptr)) {
                    cerr << "failed to parse line: " << reader.line() << endl;
                    continue;
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
                if (printHistogram) {
                    ++sizeHistogram[size];
                }
            } else if (reader.mode() == '-') {
                uintptr_t ptr = 0;
                if (!(reader >> ptr)) {
                    cerr << "failed to parse line: " << reader.line() << endl;
                    continue;
                }
                auto ip = activeAllocations.find(ptr);
                if (ip == activeAllocations.end()) {
                    if (!fromAttached) {
                        cerr << "unknown pointer in line: " << reader.line() << endl;
                    }
                    continue;
                }
                const auto info = ip->second;
                activeAllocations.erase(ip);

                auto& allocation = findAllocation(info.traceIndex);
                if (!allocation.allocations || allocation.leaked < info.size) {
                    if (!fromAttached) {
                        cerr << "inconsistent allocation info, underflowed allocations of " << info.traceIndex << endl;
                    }
                    allocation.leaked = 0;
                    allocation.allocations = 0;
                } else {
                    allocation.leaked -= info.size;
                }
                leaked -= info.size;
            } else if (reader.mode() == '#') {
                // comment or empty line
                continue;
            } else if (reader.mode() == 'c') {
                size_t newStamp = 0;
                if (!(reader >> newStamp)) {
                    cerr << "Failed to read time stamp: " << reader.line() << endl;
                    continue;
                }
                if (massifOut.is_open()) {
                    writeMassifSnapshot(timeStamp, false);
                }
                timeStamp = newStamp;
            } else if (reader.mode() == 'X') {
                cout << "Debuggee command was: " << (reader.line().c_str() + 2) << endl;
                if (massifOut.is_open()) {
                    writeMassifHeader(reader.line().c_str() + 2);
                }
            } else if (reader.mode() == 'A') {
                size_t current = 0;
                if (!(reader >> current)) {
                    cerr << "Failed to read current size after attaching." << endl;
                    continue;
                }
                leaked = current;
                peak = current;
                fromAttached = true;
            } else {
                cerr << "failed to parse line: " << reader.line() << endl;
            }
        }

        /// these are leaks, but we now have the same data in \c allocations as well
        activeAllocations.clear();

        totalTime = max(timeStamp, size_t(1));

        if (massifOut.is_open()) {
            writeMassifSnapshot(totalTime, true);
        }

        mergedAllocations = mergeAllocations(allocations);

        return true;
    }

    bool shortenTemplates = false;
    bool mergeBacktraces = true;
    bool printHistogram = false;
    bool fromAttached = false;
    ofstream massifOut;
    double massifThreshold = 1;
    size_t massifDetailedFreq = 1;

    vector<Allocation> allocations;
    vector<MergedAllocation> mergedAllocations;
    map<size_t, size_t> sizeHistogram;
    size_t totalAllocated = 0;
    size_t totalAllocations = 0;
    size_t peak = 0;
    size_t leaked = 0;
    size_t totalTime = 0;

private:
    // our indices are sequentially increasing thus a new allocation can only ever
    // occur with an index larger than any other we encountered so far
    // this can be used to our advantage in speeding up the findAllocation calls.
    TraceIndex m_maxAllocationTraceIndex;

    Allocation& findAllocation(const TraceIndex traceIndex)
    {
        if (traceIndex < m_maxAllocationTraceIndex) {
            // only need to search when the trace index is previously known
            auto it = lower_bound(allocations.begin(), allocations.end(), traceIndex,
                                [] (const Allocation& allocation, const TraceIndex traceIndex) -> bool {
                                    return allocation.traceIndex < traceIndex;
                                });
            assert(it != allocations.end());
            assert(it->traceIndex == traceIndex);
            return *it;
        } else if (traceIndex == m_maxAllocationTraceIndex && !allocations.empty()) {
            // reuse the last allocation
            assert(allocations.back().traceIndex == traceIndex);
        } else {
            // actually a new allocation
            Allocation allocation;
            allocation.traceIndex = traceIndex;
            allocations.push_back(allocation);
            m_maxAllocationTraceIndex = traceIndex;
        }
        return allocations.back();
    }

    void mergeAllocation(vector<MergedAllocation>* mergedAllocations, const Allocation& allocation) const
    {
        const auto trace = findTrace(allocation.traceIndex);
        const auto traceIp = findIp(trace.ipIndex);
        auto it = lower_bound(mergedAllocations->begin(), mergedAllocations->end(), traceIp,
                                [this] (const MergedAllocation& allocation, const InstructionPointer traceIp) -> bool {
                                    // Compare meta data without taking the instruction pointer address into account.
                                    // This is useful since sometimes, esp. when we lack debug symbols, the same function
                                    // allocates memory at different IP addresses which is pretty useless information most of the time
                                    // TODO: make this configurable, but on-by-default
                                    const auto allocationIp = findIp(allocation.ipIndex);
                                    return allocationIp.compareWithoutAddress(traceIp);
                                });
        if (it == mergedAllocations->end() || !findIp(it->ipIndex).equalWithoutAddress(traceIp)) {
            MergedAllocation merged;
            merged.ipIndex = trace.ipIndex;
            it = mergedAllocations->insert(it, merged);
        }
        it->traces.push_back(allocation);
    }

    // merge allocations so that different traces that point to the same
    // instruction pointer at the end where the allocation function is
    // called are combined
    vector<MergedAllocation> mergeAllocations(const vector<Allocation>& allocations) const
    {
        // TODO: merge deeper traces, i.e. A,B,C,D and A,B,C,F
        //       should be merged to A,B,C: D & F
        //       currently the below will only merge it to: A: B,C,D & B,C,F
        vector<MergedAllocation> ret;
        ret.reserve(allocations.size());
        for (const Allocation& allocation : allocations) {
            if (allocation.traceIndex) {
                mergeAllocation(&ret, allocation);
            }
        }
        for (MergedAllocation& merged : ret) {
            for (const Allocation& allocation: merged.traces) {
                merged.allocated += allocation.allocated;
                merged.allocations += allocation.allocations;
                merged.leaked += allocation.leaked;
                merged.peak += allocation.peak;
            }
        }
        return ret;
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

    void writeMassifHeader(const char* command)
    {
        // write massif header
        massifOut << "desc: heaptrack\n"
                  << "cmd: " << command << '\n'
                  << "time_unit: s\n";
    }

    void writeMassifSnapshot(size_t timeStamp, bool isLast)
    {
        // the heap consumption we annotate this snapshot with
        size_t heapSize = 0;
        if (peak > lastMassifPeak) {
            // if we encountered a peak in this snapshot, use that now
            // NOTE: this is wrong from a time perspective, but better than
            // ignoring the peak completely. And the error in time is at most the
            // inverse of the timer frequency, which is high.
            // FIXME: we should do something similar with the individual backtraces below
            // FIXME: non-maximum peaks after a big first one will still be hidden
            lastMassifPeak = peak;
            heapSize = lastMassifPeak;
        } else {
            heapSize = leaked;
        }

        massifOut
            << "#-----------\n"
            << "snapshot=" << massifSnapshotId << '\n'
            << "#-----------\n"
            << "time=" << (0.001 * timeStamp) << '\n'
            << "mem_heap_B=" << heapSize << '\n'
            << "mem_heap_extra_B=0\n"
            << "mem_stacks_B=0\n";

        if (massifDetailedFreq && (isLast || !(massifSnapshotId % massifDetailedFreq))) {
            massifOut << "heap_tree=detailed\n";
            const size_t threshold = double(heapSize) * massifThreshold * 0.01;
            writeMassifBacktrace(allocations, heapSize, threshold, IpIndex());
        } else {
            massifOut << "heap_tree=empty\n";
        }

        ++massifSnapshotId;
    }

    void writeMassifBacktrace(const vector<Allocation>& allocations, size_t heapSize, size_t threshold,
                              const IpIndex& location, size_t depth = 0)
    {
        size_t skippedLeaked = 0;
        size_t numAllocs = 0;
        size_t skipped = 0;
        auto mergedAllocations = mergeAllocations(allocations);
        sort(mergedAllocations.begin(), mergedAllocations.end(), [] (const MergedAllocation& l, const MergedAllocation& r) {
            return l.leaked > r.leaked;
        });

        const auto ip = findIp(location);

        const bool isMain = mainIndex && ip.functionIndex.index == mainIndex.index;

        // skip anything below main
        if (!isMain) {
            for (auto& merged : mergedAllocations) {
                if (!merged.leaked) {
                    // list is sorted, so we can bail out now - these entries are uninteresting for massif
                    break;
                }

                // skip items below threshold
                if (merged.leaked >= threshold) {
                    ++numAllocs;
                    // skip the first level of the backtrace, otherwise we'd endlessly recurse
                    for (auto& alloc : merged.traces) {
                        alloc.traceIndex = findTrace(alloc.traceIndex).parentIndex;
                    }
                } else {
                    ++skipped;
                    skippedLeaked += merged.leaked;
                }
            }
        }

        printIndent(massifOut, depth, " ");
        massifOut << 'n' << (numAllocs + (skipped ? 1 : 0)) << ": " << heapSize;
        if (!depth) {
            massifOut << " (heap allocation functions) malloc/new/new[], --alloc-fns, etc.\n";
        } else {
            massifOut << " 0x" << hex << ip.instructionPointer << dec
                      << ": ";
            if (ip.functionIndex) {
                massifOut << stringify(ip.functionIndex);
            } else {
                massifOut << "???";
            }

            massifOut << " (";
            if (ip.fileIndex) {
                massifOut << stringify(ip.fileIndex) << ':' << ip.line;
            } else if (ip.moduleIndex) {
                massifOut << stringify(ip.moduleIndex);
            } else {
                massifOut << "???";
            }
            massifOut << ")\n";
        }

        auto writeSkipped = [&] {
            if (skipped) {
                printIndent(massifOut, depth, " ");
                massifOut << " n0: " << skippedLeaked << " in " << skipped
                        << " places, all below massif's threshold (" << massifThreshold << ")\n";
                skipped = 0;
            }
        };

        if (!isMain) {
            for (const auto& merged : mergedAllocations) {
                if (merged.leaked && merged.leaked >= threshold) {
                    if (skippedLeaked > merged.leaked) {
                        // manually inject this entry to keep the output sorted
                        writeSkipped();
                    }
                    writeMassifBacktrace(merged.traces, merged.leaked, threshold, merged.ipIndex, depth + 1);
                }
            }
            writeSkipped();
        }
    }

    StringIndex mainIndex;
    unordered_map<uintptr_t, AllocationInfo> activeAllocations;
    vector<InstructionPointer> instructionPointers;
    vector<TraceNode> traces;
    vector<string> strings;
    unordered_set<size_t> opNewIpIndices;

    size_t massifSnapshotId = 0;
    size_t lastMassifPeak = 0;
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
        ("merge-backtraces,m", po::value<bool>()->default_value(true)->implicit_value(true),
            "Merge backtraces.\nNOTE: the merged peak consumption is not correct.")
        ("print-peaks,p", po::value<bool>()->default_value(true)->implicit_value(true),
            "Print backtraces to top allocators, sorted by peak consumption.")
        ("print-allocators,a", po::value<bool>()->default_value(true)->implicit_value(true),
            "Print backtraces to top allocators, sorted by number of calls to allocation functions.")
        ("print-leaks,l", po::value<bool>()->default_value(false)->implicit_value(true),
            "Print backtraces to leaked memory allocations.")
        ("print-overall-allocated,o", po::value<bool>()->default_value(false)->implicit_value(true),
            "Print top overall allocators, ignoring memory frees.")
        ("print-histogram,H", po::value<string>()->default_value(string()),
            "Path to output file where an allocation size histogram will be written to.")
        ("print-massif,M", po::value<string>()->default_value(string()),
            "Path to output file where a massif compatible data file will be written to.")
        ("massif-threshold", po::value<double>()->default_value(1.),
            "Percentage of current memory usage, below which allocations are aggregated into a 'below threshold' entry.\n"
            "This is only used in the massif output file so far.\n")
        ("massif-detailed-freq", po::value<size_t>()->default_value(2),
            "Frequency of detailed snapshots in the massif output file. Increase this to reduce the file size.\n"
            "You can set the value to zero to disable detailed snapshots.\n")
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

    const auto inputFile = vm["file"].as<string>();
    data.shortenTemplates = vm["shorten-templates"].as<bool>();
    data.mergeBacktraces = vm["merge-backtraces"].as<bool>();
    const string printHistogram = vm["print-histogram"].as<string>();
    data.printHistogram = !printHistogram.empty();
    const string printMassif = vm["print-massif"].as<string>();
    if (!printMassif.empty()) {
        data.massifOut.open(printMassif, ios_base::out);
        if (!data.massifOut.is_open())  {
            cerr << "Failed to open massif output file \"" << printMassif << "\"." << endl;
            return 1;
        }
        data.massifThreshold = vm["massif-threshold"].as<double>();
        data.massifDetailedFreq = vm["massif-detailed-freq"].as<size_t>();
    }
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
        data.printAllocations(&AllocationData::allocations, [] (const AllocationData& data) {
            cout << data.allocations << " calls to allocation functions with " << formatBytes(data.peak) << " peak consumption from\n";
        }, [] (const AllocationData& data) {
            cout << data.allocations << " calls with " << formatBytes(data.peak) << " peak consumption from:\n";
        });
        cout << endl;
    }

    if (printOverallAlloc) {
        cout << "MOST BYTES ALLOCATED OVER TIME (ignoring deallocations)\n";
        data.printAllocations(&AllocationData::allocated, [] (const AllocationData& data) {
            cout << formatBytes(data.allocated) << " allocated over " << data.allocations << " calls from\n";
        }, [] (const AllocationData& data) {
            cout << formatBytes(data.allocated) << " allocated over " << data.allocations << " calls from:\n";
        });
        cout << endl;
    }

    if (printPeaks) {
        ///FIXME: find a way to merge this without breaking temporal dependency.
        /// I.e. a given function could be called N times from different places
        /// and allocate M bytes each, but free it thereafter.
        /// Then the below would give a wrong total peak size of N * M instead
        /// of just N!
        cout << "PEAK MEMORY CONSUMERS\n";
        if (data.mergeBacktraces) {
            cout << "\nWARNING - the data below is not an accurate calcuation of"
                    " the total peak consumption and can easily be wrong.\n"
                    " For an accurate overview, disable backtrace merging.\n";
        }

        data.printAllocations(&AllocationData::peak, [] (const AllocationData& data) {
            cout << formatBytes(data.peak) << " peak memory consumed over " << data.allocations << " calls from\n";
        }, [] (const AllocationData& data) {
            cout << formatBytes(data.peak) << " consumed over " << data.allocations << " calls from:\n";
        });
    }

    if (printLeaks) {
        // sort by amount of leaks
        cout << "MEMORY LEAKS\n";
        data.printAllocations(&AllocationData::leaked, [] (const AllocationData& data) {
            cout << formatBytes(data.leaked) << " leaked over " << data.allocations << " calls from\n";
        }, [] (const AllocationData& data) {
            cout << formatBytes(data.leaked) << " leaked over " << data.allocations << " calls from:\n";
        });
        cout << endl;
    }

    const double totalTimeS = 0.001 * data.totalTime;
    cout << "total runtime: " << fixed << totalTimeS << "s.\n"
         << "bytes allocated in total (ignoring deallocations): " << formatBytes(data.totalAllocated)
            << " (" << formatBytes(data.totalAllocated / totalTimeS) << "/s)" << '\n'
         << "calls to allocation functions: " << data.totalAllocations
            << " (" << size_t(data.totalAllocations / totalTimeS) << "/s)\n"
         << "peak heap memory consumption: " << formatBytes(data.peak) << '\n'
         << "total memory leaked: " << formatBytes(data.leaked) << '\n';

    if (!printHistogram.empty()) {
        ofstream histogram(printHistogram, ios_base::out);
        if (!histogram.is_open()) {
            cerr << "Failed to open histogram output file \"" << histogram << "\"." << endl;
        } else {
            for (auto entry : data.sizeHistogram) {
                histogram << entry.first << '\t' << entry.second << '\n';
            }
        }
    }

    return 0;
}
