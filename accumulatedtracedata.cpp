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

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "linereader.h"
#include "config.h"
#include "pointermap.h"

using namespace std;

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
    stopIndices.reserve(4);
    opNewIpIndices.reserve(16);
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
    LineReader reader;
    uint64_t timeStamp = 0;

    vector<StringIndex> opNewStrIndices;
    opNewStrIndices.reserve(16);
    vector<string> opNewStrings = {
        "operator new(unsigned long)",
        "operator new[](unsigned long)"
    };

    vector<string> stopStrings = {
        "main",
        "__libc_start_main",
        "__static_initialization_and_destruction_0"
    };

    const bool reparsing = totalTime != 0;
    m_maxAllocationTraceIndex.index = 0;
    totalAllocated = 0;
    totalAllocations = 0;
    totalTemporary = 0;
    peak = 0;
    peakTime = 0;
    leaked = 0;
    systemInfo = {};
    peakRSS = 0;
    allocations.clear();
    uint fileVersion = 0;

    // required for backwards compatibility
    // newer versions handle this in heaptrack_interpret already
    AllocationInfoSet allocationInfoSet;
    PointerMap pointers;
    uint64_t lastAllocationPtr = 0;

    while (reader.getLine(in)) {
        if (reader.mode() == 's') {
            if (reparsing) {
                continue;
            }
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
            if (reparsing) {
                continue;
            }
            TraceNode node;
            reader >> node.ipIndex;
            reader >> node.parentIndex;
            // skip operator new and operator new[] at the beginning of traces
            while (find(opNewIpIndices.begin(), opNewIpIndices.end(), node.ipIndex) != opNewIpIndices.end()) {
                node = findTrace(node.parentIndex);
            }
            traces.push_back(node);
        } else if (reader.mode() == 'i') {
            if (reparsing) {
                continue;
            }
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
            AllocationInfo info;
            AllocationIndex allocationIndex;
            if (fileVersion >= 0x010000) {
                if (!(reader >> allocationIndex.index)) {
                    cerr << "failed to parse line: " << reader.line() << endl;
                    continue;
                }
                info = allocationInfos[allocationIndex.index];
            } else { // backwards compatibility
                uint64_t ptr = 0;
                if (!(reader >> info.size) || !(reader >> info.traceIndex) || !(reader >> ptr)) {
                    cerr << "failed to parse line: " << reader.line() << endl;
                    continue;
                }
                if (allocationInfoSet.add(info.size, info.traceIndex, &allocationIndex)) {
                    allocationInfos.push_back(info);
                }
                pointers.addPointer(ptr, allocationIndex);
                lastAllocationPtr = ptr;
            }

            auto& allocation = findAllocation(info.traceIndex);
            allocation.leaked += info.size;
            allocation.allocated += info.size;
            ++allocation.allocations;
            if (allocation.leaked > allocation.peak) {
                allocation.peak = allocation.leaked;
            }

            totalAllocated += info.size;
            ++totalAllocations;
            leaked += info.size;
            if (leaked > peak) {
                peak = leaked;
                peakTime = timeStamp;
            }

            handleAllocation(info, allocationIndex);
        } else if (reader.mode() == '-') {
            AllocationIndex allocationInfoIndex;
            bool temporary = false;
            if (fileVersion >= 0x010000) {
                if (!(reader >> allocationInfoIndex.index)) {
                    cerr << "failed to parse line: " << reader.line() << endl;
                    continue;
                }
                // optional, and thus may fail.
                // but that's OK since we default to false anyways
                reader >> temporary;
            } else { // backwards compatibility
                uint64_t ptr = 0;
                if (!(reader >> ptr)) {
                    cerr << "failed to parse line: " << reader.line() << endl;
                    continue;
                }
                auto taken = pointers.takePointer(ptr);
                if (!taken.second) {
                    // happens when we attached to a running application
                    continue;
                }
                allocationInfoIndex = taken.first;
                temporary = lastAllocationPtr == ptr;
                lastAllocationPtr = 0;
            }

            const auto& info = allocationInfos[allocationInfoIndex.index];
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
            if (temporary) {
                ++allocation.temporary;
                ++totalTemporary;
            }
        } else if (reader.mode() == 'a') {
            if (reparsing) {
                continue;
            }
            AllocationInfo info;
            if (!(reader >> info.size) || !(reader >> info.traceIndex)) {
                cerr << "failed to parse line: " << reader.line() << endl;
                continue;
            }
            allocationInfos.push_back(info);
        } else if (reader.mode() == '#') {
            // comment or empty line
            continue;
        } else if (reader.mode() == 'c') {
            uint64_t newStamp = 0;
            if (!(reader >> newStamp)) {
                cerr << "Failed to read time stamp: " << reader.line() << endl;
                continue;
            }
            handleTimeStamp(timeStamp, newStamp);
            timeStamp = newStamp;
        } else if (reader.mode() == 'R') { // RSS timestamp
            uint64_t rss = 0;
            reader >> rss;
            if (rss > peakRSS) {
                peakRSS = rss;
            }
        } else if (reader.mode() == 'X') {
            handleDebuggee(reader.line().c_str() + 2);
        } else if (reader.mode() == 'A') {
            leaked = 0;
            peak = 0;
            fromAttached = true;
        } else if (reader.mode() == 'v') {
            reader >> fileVersion;
            if (fileVersion > HEAPTRACK_VERSION) {
                cerr << "The data file was written by a newer heaptrack of version " << hex << fileVersion
                     << " and is thus not compatible with this build of heaptrack version " << hex << HEAPTRACK_VERSION << '.' << endl;
                return false;
            }
        } else if (reader.mode() == 'I') { // system information
            reader >> systemInfo.pageSize;
            reader >> systemInfo.pages;
        } else {
            cerr << "failed to parse line: " << reader.line() << endl;
        }
    }

    if (!reparsing) {
        totalTime = timeStamp + 1;
    }

    handleTimeStamp(timeStamp, totalTime);

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
