/*
 * Copyright 2015-2017 Milian Wolff <mail@milianw.de>
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

#include "util/linereader.h"
#include "util/config.h"
#include "util/pointermap.h"

#ifdef __GNUC__
#define POTENTIALLY_UNUSED __attribute__ ((unused))
#else
#define POTENTIALLY_UNUSED
#endif

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
    int64_t timeStamp = 0;

    vector<string> opNewStrings = {
        // 64 bit
        "operator new(unsigned long)",
        "operator new[](unsigned long)",
        // 32 bit
        "operator new(unsigned int)",
        "operator new[](unsigned int)",
    };
    vector<StringIndex> opNewStrIndices;
    opNewStrIndices.reserve(opNewStrings.size());

    vector<string> stopStrings = {
        "main",
        "__libc_start_main",
        "__static_initialization_and_destruction_0"
    };

    const bool reparsing = totalTime != 0;
    m_maxAllocationTraceIndex.index = 0;
    totalCost = {};
    peakTime = 0;
    systemInfo = {};
    peakRSS = 0;
    allocations.clear();
    uint fileVersion = 0;

    // required for backwards compatibility
    // newer versions handle this in heaptrack_interpret already
    AllocationInfoSet allocationInfoSet;
    PointerMap pointers;
    // in older files, this contains the pointer address, in newer formats
    // it holds the allocation info index. both can be used to find temporary
    // allocations, i.e. when a deallocation follows with the same data
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
                } else if (allocationIndex.index >= allocationInfos.size()) {
                    cerr << "allocation index out of bounds: " << allocationIndex.index << ", maximum is: " << allocationInfos.size() << endl;
                    continue;
                }
                info = allocationInfos[allocationIndex.index];
                lastAllocationPtr = allocationIndex.index;
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

            ++totalCost.allocations;
            totalCost.allocated += info.size;
            totalCost.leaked += info.size;
            if (totalCost.leaked > totalCost.peak) {
                totalCost.peak = totalCost.leaked;
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
                temporary = lastAllocationPtr == allocationInfoIndex.index;
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
            }
            lastAllocationPtr = 0;

            const auto& info = allocationInfos[allocationInfoIndex.index];
            auto& allocation = findAllocation(info.traceIndex);
            allocation.leaked -= info.size;
            totalCost.leaked -= info.size;
            if (temporary) {
                ++allocation.temporary;
                ++totalCost.temporary;
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
            int64_t newStamp = 0;
            if (!(reader >> newStamp)) {
                cerr << "Failed to read time stamp: " << reader.line() << endl;
                continue;
            }
            handleTimeStamp(timeStamp, newStamp);
            timeStamp = newStamp;
        } else if (reader.mode() == 'R') { // RSS timestamp
            int64_t rss = 0;
            reader >> rss;
            if (rss > peakRSS) {
                peakRSS = rss;
            }
        } else if (reader.mode() == 'X') {
            handleDebuggee(reader.line().c_str() + 2);
        } else if (reader.mode() == 'A') {
            totalCost = {};
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

namespace { // helpers for diffing

template<typename IndexT, typename SortF>
vector<IndexT> sortedIndices(size_t numIndices, SortF sorter)
{
    vector<IndexT> indices;
    indices.resize(numIndices);
    for (size_t i = 0; i < numIndices; ++i) {
        indices[i].index = (i + 1);
    }
    sort(indices.begin(), indices.end(), sorter);
    return indices;
}

vector<StringIndex> remapStrings(vector<string>& lhs, const vector<string>& rhs)
{
    unordered_map<string, StringIndex> stringRemapping;
    StringIndex stringIndex;
    {
        stringRemapping.reserve(lhs.size());
        for (const auto& string : lhs) {
            ++stringIndex.index;
            stringRemapping.insert(make_pair(string, stringIndex));
        }
    }

    vector<StringIndex> map;
    {
        map.reserve(rhs.size() + 1);
        map.push_back({});
        for (const auto& string : rhs) {
            auto it = stringRemapping.find(string);
            if (it == stringRemapping.end()) {
                ++stringIndex.index;
                lhs.push_back(string);
                map.push_back(stringIndex);
            } else {
                map.push_back(it->second);
            }
        }
    }
    return map;
}

template<typename T>
inline const T& identity(const T& t)
{
    return t;
}

template<typename IpMapper>
int compareTraceIndices(const TraceIndex &lhs, const AccumulatedTraceData& lhsData,
                          const TraceIndex &rhs, const AccumulatedTraceData& rhsData,
                          IpMapper ipMapper)
{
    if (!lhs && !rhs) {
        return 0;
    } else if (lhs && !rhs) {
        return 1;
    } else if (rhs && !lhs) {
        return -1;
    } else if (&lhsData == &rhsData && lhs == rhs) {
        // fast-path if both indices are equal and we compare the same data
        return 0;
    }

    const auto& lhsTrace = lhsData.findTrace(lhs);
    const auto& rhsTrace = rhsData.findTrace(rhs);

    const int parentComparsion = compareTraceIndices(lhsTrace.parentIndex, lhsData, rhsTrace.parentIndex, rhsData, ipMapper);
    if (parentComparsion != 0) {
        return parentComparsion;
    } // else fall-through to below, parents are equal

    const auto& lhsIp = lhsData.findIp(lhsTrace.ipIndex);
    const auto& rhsIp = ipMapper(rhsData.findIp(rhsTrace.ipIndex));
    if (lhsIp.equalWithoutAddress(rhsIp)) {
        return 0;
    }
    return lhsIp.compareWithoutAddress(rhsIp) ? -1 : 1;
}

POTENTIALLY_UNUSED void printTrace(const AccumulatedTraceData& data, TraceIndex index)
{
    do {
        const auto trace = data.findTrace(index);
        const auto& ip = data.findIp(trace.ipIndex);
        cerr << index << " (" << trace.ipIndex << ", " << trace.parentIndex << ")"
            << '\t' << data.stringify(ip.functionIndex)
            << " in " << data.stringify(ip.moduleIndex)
            << " at " << data.stringify(ip.fileIndex) << ':' << ip.line
            << '\n';
        index = trace.parentIndex;
    } while (index);
    cerr << "---\n";
}
}

void AccumulatedTraceData::diff(const AccumulatedTraceData& base)
{
    totalCost -= base.totalCost;
    totalTime -= base.totalTime;
    peakRSS -= base.peakRSS;
    systemInfo.pages -= base.systemInfo.pages;
    systemInfo.pageSize -= base.systemInfo.pageSize;

    // step 1: sort trace indices of allocations for efficient lookup
    // step 2: while at it, also merge equal allocations
    vector<TraceIndex> allocationTraceNodes;
    allocationTraceNodes.reserve(allocations.size());
    for (auto it = allocations.begin(); it != allocations.end();) {
        const auto& allocation = *it;
        auto sortedIt = lower_bound(allocationTraceNodes.begin(), allocationTraceNodes.end(), allocation.traceIndex,
            [this] (const TraceIndex& lhs, const TraceIndex& rhs) -> bool {
                return compareTraceIndices(lhs, *this,
                                           rhs, *this,
                                           identity<InstructionPointer>) < 0;
            });
        if (sortedIt == allocationTraceNodes.end()
            || compareTraceIndices(allocation.traceIndex, *this, *sortedIt, *this, identity<InstructionPointer>) != 0)
        {
            allocationTraceNodes.insert(sortedIt, allocation.traceIndex);
            ++it;
        } else if (*sortedIt != allocation.traceIndex) {
            findAllocation(*sortedIt) += allocation;
            it = allocations.erase(it);
        } else {
            ++it;
        }
    }

    // step 3: map string indices from rhs to lhs data

    const auto& stringMap = remapStrings(strings, base.strings);
    auto remapString = [&stringMap] (StringIndex& index) {
        if (index) {
            index.index = stringMap[index.index].index;
        }
    };
    auto remapIp = [&remapString] (InstructionPointer ip) -> InstructionPointer {
        remapString(ip.moduleIndex);
        remapString(ip.functionIndex);
        remapString(ip.fileIndex);
        return ip;
    };

    // step 4: iterate over rhs data and find matching traces
    //         if no match is found, copy the data over

    auto sortedIps = sortedIndices<IpIndex>(instructionPointers.size(),
        [this] (const IpIndex &lhs, const IpIndex &rhs) {
            return findIp(lhs).compareWithoutAddress(findIp(rhs));
        });

    // map an IpIndex from the rhs data into the lhs data space, or copy the data
    // if it does not exist yet
    auto remapIpIndex = [&sortedIps, this, &base, &remapIp] (IpIndex rhsIndex) -> IpIndex {
        if (!rhsIndex) {
            return rhsIndex;
        }

        const auto& rhsIp = base.findIp(rhsIndex);
        const auto& lhsIp = remapIp(rhsIp);

        auto it = lower_bound(sortedIps.begin(), sortedIps.end(), lhsIp,
                              [this] (const IpIndex &lhs, const InstructionPointer &rhs) {
                                  return findIp(lhs).compareWithoutAddress(rhs);
                              });
        if (it != sortedIps.end() && findIp(*it).equalWithoutAddress(lhsIp)) {
            return *it;
        }

        instructionPointers.push_back(lhsIp);

        IpIndex ret;
        ret.index = instructionPointers.size();
        sortedIps.insert(it, ret);

        return ret;
    };

    // copy the rhs trace index and the data it references into the lhs data, recursively
    function<TraceIndex (TraceIndex)> copyTrace = [this, &base, remapIpIndex, &copyTrace] (TraceIndex rhsIndex) -> TraceIndex {
        if (!rhsIndex) {
            return rhsIndex;
        }

        // new location, add it
        const auto& rhsTrace = base.findTrace(rhsIndex);

        TraceNode node;
        node.parentIndex = copyTrace(rhsTrace.parentIndex);
        node.ipIndex = remapIpIndex(rhsTrace.ipIndex);

        traces.push_back(node);
        TraceIndex ret;
        ret.index = traces.size();

        return ret;
    };

    // find an equivalent trace or copy the data over if it does not exist yet
    // a trace is equivalent if the complete backtrace has equal InstructionPointer
    // data while ignoring the actual pointer address
    auto remapTrace = [&base, &allocationTraceNodes, this, remapIp, copyTrace] (TraceIndex rhsIndex) -> TraceIndex {
        if (!rhsIndex) {
            return rhsIndex;
        }

        auto it = lower_bound(allocationTraceNodes.begin(), allocationTraceNodes.end(), rhsIndex,
            [&base, this, remapIp] (const TraceIndex& lhs, const TraceIndex& rhs) -> bool {
                return compareTraceIndices(lhs, *this, rhs, base, remapIp) < 0;
            });

        if (it != allocationTraceNodes.end()
            && compareTraceIndices(*it, *this, rhsIndex, base, remapIp) == 0)
        {
            return *it;
        }

        TraceIndex ret = copyTrace(rhsIndex);
        allocationTraceNodes.insert(it, ret);
        return ret;
    };

    for (const auto& rhsAllocation : base.allocations) {
        const auto lhsTrace = remapTrace(rhsAllocation.traceIndex);
        assert(base.findIp(base.findTrace(rhsAllocation.traceIndex).ipIndex).equalWithoutAddress(findIp(findTrace(lhsTrace).ipIndex)));
        findAllocation(lhsTrace) -= rhsAllocation;
    }

    // step 5: remove allocations that don't show any differences
    //         note that when there are differences in the backtraces,
    //         we can still end up with merged backtraces that have a total
    //         of 0, but different "tails" of different origin with non-zero cost
    allocations.erase(remove_if(allocations.begin(), allocations.end(),
        [] (const Allocation& allocation) -> bool {
            return allocation == AllocationData();
        }), allocations.end());
}

Allocation& AccumulatedTraceData::findAllocation(const TraceIndex traceIndex)
{
    if (traceIndex < m_maxAllocationTraceIndex) {
        // only need to search when the trace index is previously known
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
