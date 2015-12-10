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

#ifndef ACCUMULATEDTRACEDATA_H
#define ACCUMULATEDTRACEDATA_H

#include <iosfwd>
#include <tuple>
#include <vector>

#include <fstream>
#include <unordered_map>
#include <map>

// sadly, C++ doesn't yet have opaque typedefs
template<typename Base>
struct Index
{
    uint32_t index = 0;

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
uint qHash(const Index<Base> index, uint seed = 0) noexcept
{
    return qHash(index.index, seed);
}

struct StringIndex : public Index<StringIndex> {};
struct ModuleIndex : public StringIndex {};
struct FunctionIndex : public StringIndex {};
struct FileIndex : public StringIndex {};
struct IpIndex : public Index<IpIndex> {};
struct TraceIndex : public Index<TraceIndex> {};

struct InstructionPointer
{
    uint64_t instructionPointer = 0;
    ModuleIndex moduleIndex;
    FunctionIndex functionIndex;
    FileIndex fileIndex;
    int line = 0;

    bool compareWithoutAddress(const InstructionPointer &other) const
    {
        return std::make_tuple(moduleIndex, functionIndex, fileIndex, line)
             < std::make_tuple(other.moduleIndex, other.functionIndex, other.fileIndex, other.line);
    }

    bool equalWithoutAddress(const InstructionPointer &other) const
    {
        return std::make_tuple(moduleIndex, functionIndex, fileIndex, line)
            == std::make_tuple(other.moduleIndex, other.functionIndex, other.fileIndex, other.line);
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
    uint64_t allocations = 0;
    // bytes allocated in total
    uint64_t allocated = 0;
    // amount of bytes leaked
    uint64_t leaked = 0;
    // largest amount of bytes allocated
    uint64_t peak = 0;
    // number of temporary allocations
    uint64_t temporary = 0;
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
    std::vector<Allocation> traces;
    // location
    IpIndex ipIndex;
};

/**
 * Information for a single call to an allocation function for small allocations.
 *
 * The split between small and big allocations is done to save memory. Most of
 * the time apps will only do small allocations, and tons of them. With this
 * split we can reduce the memory footprint of the active allocation tracker
 * below by a factor of 2. This is especially notable for apps that do tons
 * of small allocations and don't free them. A notable example for such an
 * application is heaptrack_print/heaptrack_gui itself!
 */
struct SmallAllocationInfo
{
    TraceIndex traceIndex;
    uint32_t size;
};

/**
 * Information for a single call to an allocation function for big allocations.
 */
struct BigAllocationInfo
{
    TraceIndex traceIndex;
    uint64_t size;
};

struct AccumulatedTraceData
{
    AccumulatedTraceData();
    virtual ~AccumulatedTraceData() = default;

    virtual void handleTimeStamp(uint64_t oldStamp, uint64_t newStamp) = 0;
    virtual void handleAllocation() = 0;
    virtual void handleDebuggee(const char* command) = 0;

    const std::string& stringify(const StringIndex stringId) const;

    std::string prettyFunction(const std::string& function) const;

    bool read(const std::string& inputFile);
    bool read(std::istream& in);

    bool shortenTemplates = false;
    bool printHistogram = false;
    bool fromAttached = false;

    std::vector<Allocation> allocations;
    std::map<uint64_t, uint64_t> sizeHistogram;
    uint64_t totalAllocated = 0;
    uint64_t totalAllocations = 0;
    uint64_t totalTemporary = 0;
    uint64_t peak = 0;
    uint64_t leaked = 0;
    uint64_t totalTime = 0;

    // our indices are sequentially increasing thus a new allocation can only ever
    // occur with an index larger than any other we encountered so far
    // this can be used to our advantage in speeding up the findAllocation calls.
    TraceIndex m_maxAllocationTraceIndex;

    Allocation& findAllocation(const TraceIndex traceIndex);

    InstructionPointer findIp(const IpIndex ipIndex) const;

    TraceNode findTrace(const TraceIndex traceIndex) const;

    bool isStopIndex(const StringIndex index) const;

    BigAllocationInfo takeActiveAllocation(uint64_t ptr);

    // indices of functions that should stop the backtrace, e.g. main or static initialization
    std::vector<StringIndex> stopIndices;
    std::unordered_map<uint64_t, SmallAllocationInfo> activeSmallAllocations;
    std::unordered_map<uint64_t, BigAllocationInfo> activeBigAllocations;
    std::vector<InstructionPointer> instructionPointers;
    std::vector<TraceNode> traces;
    std::vector<std::string> strings;
    std::vector<IpIndex> opNewIpIndices;
};

#endif // ACCUMULATEDTRACEDATA_H
