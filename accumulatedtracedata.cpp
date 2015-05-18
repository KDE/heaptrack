/*
 * Copyright 2015 Milian Wolff <mail@milianw.de>
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

#include "accumulatedtracedata.h"

#include <iostream>
#include <memory>
#include <algorithm>
#include <cassert>
#include <iomanip>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "linereader.h"

using namespace std;

ostream& operator<<(ostream& out, const formatBytes data)
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

namespace {

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

}

AccumulatedTraceData::AccumulatedTraceData()
{
    instructionPointers.reserve(16384);
    traces.reserve(65536);
    strings.reserve(4096);
    allocations.reserve(16384);
    activeAllocations.reserve(65536);
    stopIndices.reserve(4);
}

void AccumulatedTraceData::clear()
{
    stopIndices.clear();
    instructionPointers.clear();
    traces.clear();
    strings.clear();
    mergedAllocations.clear();
    allocations.clear();
    activeAllocations.clear();
}

void AccumulatedTraceData::handleTimeStamp(size_t /*newStamp*/, size_t /*oldStamp*/)
{
}

void AccumulatedTraceData::handleAllocation()
{
}

void AccumulatedTraceData::handleDebuggee(const char* command)
{
}

const string& AccumulatedTraceData::stringify(const StringIndex stringId) const
{
    if (!stringId || stringId.index > strings.size()) {
        static const string empty;
        return empty;
    } else {
        return strings.at(stringId.index - 1);
    }
}

string AccumulatedTraceData::prettyFunction(const string& function) const
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

bool AccumulatedTraceData::read(const string& inputFile)
{
    const bool isCompressed = boost::algorithm::ends_with(inputFile, ".gz");
    ifstream file(inputFile, isCompressed ? ios_base::in | ios_base::binary : ios_base::in);

    if (!file.is_open()) {
        cerr << "Failed to open heaptrack log file: " << inputFile << endl;
        return false;
    }

    boost::iostreams::filtering_istream in;
    if (isCompressed) {
        in.push(boost::iostreams::gzip_decompressor());
    }
    in.push(file);
    return read(in);
}

bool AccumulatedTraceData::read(istream& in)
{
    clear();

    LineReader reader;
    size_t timeStamp = 0;

    vector<StringIndex> opNewStrIndices;
    opNewStrIndices.reserve(16);
    vector<IpIndex> opNewIpIndices;
    opNewIpIndices.reserve(16);
    vector<string> opNewStrings = {
        "operator new(unsigned long)",
        "operator new[](unsigned long)"
    };

    vector<string> stopStrings = {
        "main",
        "__libc_start_main",
        "__static_initialization_and_destruction_0"
    };

    while (reader.getLine(in)) {
        if (reader.mode() == 's') {
            strings.push_back(reader.line().substr(2));
            StringIndex index;
            index.index = strings.size();

            auto opNewIt = find(opNewStrings.begin(), opNewStrings.end(), strings.back());
            if (opNewIt != opNewStrings.end()) {
                opNewStrIndices.push_back(index);
                opNewStrings.erase(opNewIt);
            } else {
                auto stopIt = find(stopStrings.begin(), stopStrings.end(), strings.back());
                if (stopIt != stopStrings.end()) {
                    stopIndices.push_back(index);
                    stopStrings.erase(stopIt);
                }
            }
        } else if (reader.mode() == 't') {
            TraceNode node;
            reader >> node.ipIndex;
            reader >> node.parentIndex;
            // skip operator new and operator new[] at the beginning of traces
            while (find(opNewIpIndices.begin(), opNewIpIndices.end(), node.ipIndex) != opNewIpIndices.end()) {
                node = findTrace(node.parentIndex);
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
            if (find(opNewStrIndices.begin(), opNewStrIndices.end(), ip.functionIndex) != opNewStrIndices.end()) {
                IpIndex index;
                index.index = instructionPointers.size();
                opNewIpIndices.push_back(index);
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
            handleAllocation();
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
            handleTimeStamp(timeStamp, newStamp);
            timeStamp = newStamp;
        } else if (reader.mode() == 'X') {
            cout << "Debuggee command was: " << (reader.line().c_str() + 2) << endl;
            handleDebuggee(reader.line().c_str() + 2);
        } else if (reader.mode() == 'A') {
            leaked = 0;
            peak = 0;
            fromAttached = true;
        } else {
            cerr << "failed to parse line: " << reader.line() << endl;
        }
    }

    /// these are leaks, but we now have the same data in \c allocations as well
    activeAllocations.clear();

    totalTime = max(timeStamp, size_t(1));

    handleTimeStamp(timeStamp, totalTime);

    filterAllocations();
    mergedAllocations = mergeAllocations(allocations);

    return true;
}

Allocation& AccumulatedTraceData::findAllocation(const TraceIndex traceIndex)
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

void AccumulatedTraceData::mergeAllocation(vector<MergedAllocation>* mergedAllocations, const Allocation& allocation) const
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
vector<MergedAllocation> AccumulatedTraceData::mergeAllocations(const vector<Allocation>& allocations) const
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

InstructionPointer AccumulatedTraceData::findIp(const IpIndex ipIndex) const
{
    if (!ipIndex || ipIndex.index > instructionPointers.size()) {
        return {};
    } else {
        return instructionPointers[ipIndex.index - 1];
    }
}

TraceNode AccumulatedTraceData::findTrace(const TraceIndex traceIndex) const
{
    if (!traceIndex || traceIndex.index > traces.size()) {
        return {};
    } else {
        return traces[traceIndex.index - 1];
    }
}

bool AccumulatedTraceData::isStopIndex(const StringIndex index) const
{
    return find(stopIndices.begin(), stopIndices.end(), index) != stopIndices.end();
}

void AccumulatedTraceData::filterAllocations()
{
    if (filterBtFunction.empty()) {
        return;
    }
    allocations.erase(remove_if(allocations.begin(), allocations.end(), [&] (const Allocation& allocation) -> bool {
        auto node = findTrace(allocation.traceIndex);
        while (node.ipIndex) {
            const auto& ip = findIp(node.ipIndex);
            if (isStopIndex(ip.functionIndex)) {
                break;
            }
            if (stringify(ip.functionIndex).find(filterBtFunction) != string::npos) {
                return false;
            }
            node = findTrace(node.parentIndex);
        };
        return true;
    }), allocations.end());
}
