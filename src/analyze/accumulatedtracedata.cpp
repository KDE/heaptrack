/*
 * Copyright 2015-2017 Milian Wolff <mail@milianw.de>
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

#include "accumulatedtracedata.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>

#include <boost-zstd/zstd.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_stream.hpp>

#include "util/config.h"
#include "util/linereader.h"
#include "util/pointermap.h"

#include "analyze_config.h"

#ifdef __GNUC__
#define POTENTIALLY_UNUSED __attribute__((unused))
#else
#define POTENTIALLY_UNUSED
#endif

using namespace std;

namespace {

template <typename Base>
bool operator>>(LineReader& reader, Index<Base>& index)
{
    return reader.readHex(index.index);
}

template <typename Base>
ostream& operator<<(ostream& out, const Index<Base> index)
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
    traceIndexToAllocationIndex.reserve(16384);
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
    if (totalTime == 0) {
        return read(inputFile, FirstPass) && read(inputFile, SecondPass);
    } else {
        return read(inputFile, ThirdPass);
    }
}

bool AccumulatedTraceData::read(const string& inputFile, const ParsePass pass)
{
    const bool isGzCompressed = boost::algorithm::ends_with(inputFile, ".gz");
    const bool isZstdCompressed = boost::algorithm::ends_with(inputFile, ".zst");
    const bool isCompressed = isGzCompressed || isZstdCompressed;
    ifstream file(inputFile, isCompressed ? ios_base::in | ios_base::binary : ios_base::in);

    if (!file.is_open()) {
        cerr << "Failed to open heaptrack log file: " << inputFile << endl;
        return false;
    }

    boost::iostreams::filtering_istream in;
    if (isGzCompressed) {
        in.push(boost::iostreams::gzip_decompressor());
    } else if (isZstdCompressed) {
#if ZSTD_FOUND
        in.push(boost::iostreams::zstd_decompressor());
#else
        cerr << "Heaptrack was built without zstd support, cannot decompressed data file: " << inputFile << endl;
        return false;
#endif
    }
    in.push(file);

    return read(in, pass);
}

bool AccumulatedTraceData::read(istream& in, const ParsePass pass)
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

    vector<string> stopStrings = {"main", "__libc_start_main", "__static_initialization_and_destruction_0"};

    const auto lastPeakCost = pass != FirstPass ? totalCost.peak : 0;
    const auto lastPeakTime = pass != FirstPass ? peakTime : 0;

    totalCost = {};
    peakTime = 0;
    systemInfo = {};
    peakRSS = 0;
    for (auto& allocation : allocations) {
        allocation.clearCost();
    }
    unsigned int fileVersion = 0;
    bool debuggeeEncountered = false;

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
            if (pass != FirstPass) {
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
            if (pass != FirstPass) {
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
            if (pass != FirstPass) {
                continue;
            }
            InstructionPointer ip;
            reader >> ip.instructionPointer;
            reader >> ip.moduleIndex;
            auto readFrame = [&reader](Frame* frame) {
                return (reader >> frame->functionIndex) && (reader >> frame->fileIndex) && (reader >> frame->line);
            };
            if (readFrame(&ip.frame)) {
                Frame inlinedFrame;
                while (readFrame(&inlinedFrame)) {
                    ip.inlined.push_back(inlinedFrame);
                }
            }

            instructionPointers.push_back(ip);
            if (find(opNewStrIndices.begin(), opNewStrIndices.end(), ip.frame.functionIndex) != opNewStrIndices.end()) {
                IpIndex index;
                index.index = instructionPointers.size();
                opNewIpIndices.push_back(index);
            }
        } else if (reader.mode() == '+') {
            AllocationInfo info;
            AllocationInfoIndex allocationIndex;
            if (fileVersion >= 1) {
                if (!(reader >> allocationIndex)) {
                    cerr << "failed to parse line: " << reader.line() << endl;
                    continue;
                } else if (allocationIndex.index >= allocationInfos.size()) {
                    cerr << "allocation index out of bounds: " << allocationIndex
                         << ", maximum is: " << allocationInfos.size() << endl;
                    continue;
                }
                info = allocationInfos[allocationIndex.index];
                lastAllocationPtr = allocationIndex.index;
            } else { // backwards compatibility
                uint64_t ptr = 0;
                TraceIndex traceIndex;
                if (!(reader >> info.size) || !(reader >> traceIndex) || !(reader >> ptr)) {
                    cerr << "failed to parse line: " << reader.line() << endl;
                    continue;
                }
                info.allocationIndex = mapToAllocationIndex(traceIndex);
                if (allocationInfoSet.add(info.size, traceIndex, &allocationIndex)) {
                    allocationInfos.push_back(info);
                }
                pointers.addPointer(ptr, allocationIndex);
                lastAllocationPtr = ptr;
            }

            if (pass != FirstPass) {
                auto& allocation = allocations[info.allocationIndex.index];
                allocation.leaked += info.size;
                ++allocation.allocations;

                handleAllocation(info, allocationIndex);
            }

            ++totalCost.allocations;
            totalCost.leaked += info.size;
            if (totalCost.leaked > totalCost.peak) {
                totalCost.peak = totalCost.leaked;
                peakTime = timeStamp;

                if (pass == SecondPass && totalCost.peak == lastPeakCost && peakTime == lastPeakTime) {
                    for (auto& allocation : allocations) {
                        allocation.peak = allocation.leaked;
                    }
                }
            }
        } else if (reader.mode() == '-') {
            AllocationInfoIndex allocationInfoIndex;
            bool temporary = false;
            if (fileVersion >= 1) {
                if (!(reader >> allocationInfoIndex)) {
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
            totalCost.leaked -= info.size;
            if (temporary) {
                ++totalCost.temporary;
            }

            if (pass != FirstPass) {
                auto& allocation = allocations[info.allocationIndex.index];
                allocation.leaked -= info.size;
                if (temporary) {
                    ++allocation.temporary;
                }
            }
        } else if (reader.mode() == 'a') {
            if (pass != FirstPass) {
                continue;
            }
            AllocationInfo info;
            TraceIndex traceIndex;
            if (!(reader >> info.size) || !(reader >> traceIndex)) {
                cerr << "failed to parse line: " << reader.line() << endl;
                continue;
            }
            info.allocationIndex = mapToAllocationIndex(traceIndex);
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
            if (pass != FirstPass) {
                handleTimeStamp(timeStamp, newStamp);
            }
            timeStamp = newStamp;
        } else if (reader.mode() == 'R') { // RSS timestamp
            int64_t rss = 0;
            reader >> rss;
            if (rss > peakRSS) {
                peakRSS = rss;
            }
        } else if (reader.mode() == 'X') {
            if (debuggeeEncountered) {
                cerr << "Duplicated debuggee entry - corrupt data file?" << endl;
                return false;
            }
            debuggeeEncountered = true;
            if (pass != FirstPass) {
                handleDebuggee(reader.line().c_str() + 2);
            }
        } else if (reader.mode() == 'A') {
            totalCost = {};
            fromAttached = true;
        } else if (reader.mode() == 'v') {
            unsigned int heaptrackVersion = 0;
            reader >> heaptrackVersion;
            if (!(reader >> fileVersion) && heaptrackVersion == 0x010200) {
                // backwards compatibility: before the 1.0.0, I actually
                // bumped the version to 0x010200 already and used that
                // as file version. This is what we now consider v1 of the
                // file format
                fileVersion = 1;
            }
            if (fileVersion > HEAPTRACK_FILE_FORMAT_VERSION) {
                cerr << "The data file has version " << hex << fileVersion << " and was written by heaptrack version "
                     << hex << heaptrackVersion << ")\n"
                     << "This is not compatible with this build of heaptrack (version " << hex << HEAPTRACK_VERSION
                     << "), which can read file format version " << hex << HEAPTRACK_FILE_FORMAT_VERSION << " and below"
                     << endl;
                return false;
            }
        } else if (reader.mode() == 'I') { // system information
            reader >> systemInfo.pageSize;
            reader >> systemInfo.pages;
        } else {
            cerr << "failed to parse line: " << reader.line() << endl;
        }
    }

    if (pass == FirstPass) {
        totalTime = timeStamp + 1;
    } else {
        handleTimeStamp(timeStamp, totalTime);
    }

    return true;
}

namespace { // helpers for diffing

template <typename IndexT, typename SortF>
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

template <typename T>
inline T identity(const T& t)
{
    return t;
}

template <typename IpMapper>
int compareTraceIndices(const TraceIndex& lhs, const AccumulatedTraceData& lhsData, const TraceIndex& rhs,
                        const AccumulatedTraceData& rhsData, IpMapper ipMapper)
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

    const int parentComparsion =
        compareTraceIndices(lhsTrace.parentIndex, lhsData, rhsTrace.parentIndex, rhsData, ipMapper);
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
        cerr << index << " (" << trace.ipIndex << ", " << trace.parentIndex << ")" << '\t'
             << data.stringify(ip.frame.functionIndex) << " in " << data.stringify(ip.moduleIndex) << " at "
             << data.stringify(ip.frame.fileIndex) << ':' << ip.frame.line << '\n';
        for (const auto& inlined : ip.inlined) {
            cerr << '\t' << data.stringify(inlined.functionIndex) << " at " << data.stringify(inlined.fileIndex) << ':'
                 << inlined.line << '\n';
        }
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
        auto sortedIt =
            lower_bound(allocationTraceNodes.begin(), allocationTraceNodes.end(), allocation.traceIndex,
                        [this](const TraceIndex& lhs, const TraceIndex& rhs) -> bool {
                            return compareTraceIndices(lhs, *this, rhs, *this, identity<InstructionPointer>) < 0;
                        });
        if (sortedIt == allocationTraceNodes.end()
            || compareTraceIndices(allocation.traceIndex, *this, *sortedIt, *this, identity<InstructionPointer>) != 0) {
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
    auto remapString = [&stringMap](StringIndex& index) {
        if (index) {
            index.index = stringMap[index.index].index;
        }
    };
    auto remapFrame = [&remapString](Frame frame) -> Frame {
        remapString(frame.functionIndex);
        remapString(frame.fileIndex);
        return frame;
    };
    auto remapIp = [&remapString, &remapFrame](InstructionPointer ip) -> InstructionPointer {
        remapString(ip.moduleIndex);
        ip.frame = remapFrame(ip.frame);
        for (auto& inlined : ip.inlined) {
            inlined = remapFrame(inlined);
        }
        return ip;
    };

    // step 4: iterate over rhs data and find matching traces
    //         if no match is found, copy the data over

    auto sortedIps = sortedIndices<IpIndex>(instructionPointers.size(), [this](const IpIndex& lhs, const IpIndex& rhs) {
        return findIp(lhs).compareWithoutAddress(findIp(rhs));
    });

    // map an IpIndex from the rhs data into the lhs data space, or copy the data
    // if it does not exist yet
    auto remapIpIndex = [&sortedIps, this, &base, &remapIp](IpIndex rhsIndex) -> IpIndex {
        if (!rhsIndex) {
            return rhsIndex;
        }

        const auto& rhsIp = base.findIp(rhsIndex);
        const auto& lhsIp = remapIp(rhsIp);

        auto it = lower_bound(sortedIps.begin(), sortedIps.end(), lhsIp,
                              [this](const IpIndex& lhs, const InstructionPointer& rhs) {
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

    // copy the rhs trace index and the data it references into the lhs data,
    // recursively
    function<TraceIndex(TraceIndex)> copyTrace = [this, &base, remapIpIndex,
                                                  &copyTrace](TraceIndex rhsIndex) -> TraceIndex {
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
    // a trace is equivalent if the complete backtrace has equal
    // InstructionPointer
    // data while ignoring the actual pointer address
    auto remapTrace = [&base, &allocationTraceNodes, this, remapIp, copyTrace](TraceIndex rhsIndex) -> TraceIndex {
        if (!rhsIndex) {
            return rhsIndex;
        }

        auto it = lower_bound(allocationTraceNodes.begin(), allocationTraceNodes.end(), rhsIndex,
                              [&base, this, remapIp](const TraceIndex& lhs, const TraceIndex& rhs) -> bool {
                                  return compareTraceIndices(lhs, *this, rhs, base, remapIp) < 0;
                              });

        if (it != allocationTraceNodes.end() && compareTraceIndices(*it, *this, rhsIndex, base, remapIp) == 0) {
            return *it;
        }

        TraceIndex ret = copyTrace(rhsIndex);
        allocationTraceNodes.insert(it, ret);
        return ret;
    };

    for (const auto& rhsAllocation : base.allocations) {
        const auto lhsTrace = remapTrace(rhsAllocation.traceIndex);
        assert(remapIp(base.findIp(base.findTrace(rhsAllocation.traceIndex).ipIndex))
                   .equalWithoutAddress(findIp(findTrace(lhsTrace).ipIndex)));
        findAllocation(lhsTrace) -= rhsAllocation;
    }

    // step 5: remove allocations that don't show any differences
    //         note that when there are differences in the backtraces,
    //         we can still end up with merged backtraces that have a total
    //         of 0, but different "tails" of different origin with non-zero cost
    allocations.erase(remove_if(allocations.begin(), allocations.end(),
                                [](const Allocation& allocation) -> bool { return allocation == AllocationData(); }),
                      allocations.end());
}

AllocationIndex AccumulatedTraceData::mapToAllocationIndex(const TraceIndex traceIndex)
{
    AllocationIndex allocationIndex;
    if (traceIndex < m_maxAllocationTraceIndex) {
        // only need to search when the trace index is previously known
        auto it = lower_bound(traceIndexToAllocationIndex.begin(), traceIndexToAllocationIndex.end(), traceIndex,
                              [](const pair<TraceIndex, AllocationIndex>& indexMap,
                                 const TraceIndex traceIndex) -> bool { return indexMap.first < traceIndex; });
        if (it != traceIndexToAllocationIndex.end() && it->first == traceIndex) {
            return it->second;
        }
        // new allocation
        allocationIndex.index = allocations.size();
        traceIndexToAllocationIndex.insert(it, make_pair(traceIndex, allocationIndex));
        Allocation allocation;
        allocation.traceIndex = traceIndex;
        allocations.push_back(allocation);
    } else if (traceIndex == m_maxAllocationTraceIndex && !allocations.empty()) {
        // reuse the last allocation
        assert(allocations.back().traceIndex == traceIndex);
        allocationIndex.index = allocations.size() - 1;
    } else {
        // new allocation
        allocationIndex.index = allocations.size();
        traceIndexToAllocationIndex.push_back(make_pair(traceIndex, allocationIndex));
        Allocation allocation;
        allocation.traceIndex = traceIndex;
        allocations.push_back(allocation);
        m_maxAllocationTraceIndex = traceIndex;
    }
    return allocationIndex;
}

Allocation& AccumulatedTraceData::findAllocation(const TraceIndex traceIndex)
{
    return allocations[mapToAllocationIndex(traceIndex).index];
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
