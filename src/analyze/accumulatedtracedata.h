/*
    SPDX-FileCopyrightText: 2015-2020 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef ACCUMULATEDTRACEDATA_H
#define ACCUMULATEDTRACEDATA_H

#include <iosfwd>
#include <tuple>
#include <vector>

#include <fstream>

#include <boost/iostreams/filtering_stream.hpp>

#include "allocationdata.h"
#include "filterparameters.h"
#include "util/indices.h"

struct Frame
{
    FunctionIndex functionIndex;
    FileIndex fileIndex;
    int line = 0;

    bool operator==(const Frame& rhs) const
    {
        return functionIndex == rhs.functionIndex && fileIndex == rhs.fileIndex && line == rhs.line;
    }

    bool operator<(const Frame& rhs) const
    {
        return std::tie(functionIndex, fileIndex, line) < std::tie(rhs.functionIndex, rhs.fileIndex, rhs.line);
    }
};

struct InstructionPointer
{
    uint64_t instructionPointer = 0;
    ModuleIndex moduleIndex;
    Frame frame;
    std::vector<Frame> inlined;

    bool compareWithoutAddress(const InstructionPointer& other) const
    {
        return std::tie(moduleIndex, frame) < std::tie(other.moduleIndex, other.frame);
    }

    bool equalWithoutAddress(const InstructionPointer& other) const
    {
        return moduleIndex == other.moduleIndex && frame == other.frame;
    }
};

struct TraceNode
{
    IpIndex ipIndex;
    TraceIndex parentIndex;
};

struct Allocation : public AllocationData
{
    // backtrace entry point
    TraceIndex traceIndex;
};

/**
 * Information for a single call to an allocation function.
 */
struct AllocationInfo
{
    uint64_t size = 0;
    // index into AccumulatedTraceData::allocations
    AllocationIndex allocationIndex;

    bool operator==(const AllocationInfo& rhs) const
    {
        return rhs.allocationIndex == allocationIndex && rhs.size == size;
    }
};

struct Suppression;

struct AccumulatedTraceData
{
    AccumulatedTraceData();
    virtual ~AccumulatedTraceData();

    enum ParsePass
    {
        // parse individual allocations
        FirstPass,
        // GUI only: graph-building
        SecondPass
    };

    virtual void handleTimeStamp(int64_t oldStamp, int64_t newStamp, bool isFinalTimeStamp, const ParsePass pass) = 0;
    virtual void handleAllocation(const AllocationInfo& info, const AllocationInfoIndex index) = 0;
    virtual void handleDebuggee(const char* command) = 0;

    const std::string& stringify(const StringIndex stringId) const;

    std::string prettyFunction(const std::string& function) const;

    bool read(const std::string& inputFile, bool isReparsing);
    bool read(const std::string& inputFile, const ParsePass pass, bool isReparsing);
    bool read(boost::iostreams::filtering_istream& in, const ParsePass pass, bool isReparsing);

    void diff(const AccumulatedTraceData& base);

    bool shortenTemplates = false;
    bool fromAttached = false;
    FilterParameters filterParameters;

    std::vector<Allocation> allocations;
    AllocationData totalCost;
    int64_t totalTime = 0;
    int64_t peakTime = 0;
    int64_t peakRSS = 0;

    struct SystemInfo
    {
        int64_t pages = 0;
        int64_t pageSize = 0;
    };
    SystemInfo systemInfo;

    // our indices are sequentially increasing thus a new allocation can only ever
    // occur with an index larger than any other we encountered so far
    // this can be used to our advantage in speeding up the mapToAllocationIndex calls.
    TraceIndex m_maxAllocationTraceIndex;
    AllocationIndex m_maxAllocationIndex;
    // we don't want to shuffle allocations around, so instead keep a secondary
    // vector around for efficient index lookup
    std::vector<std::pair<TraceIndex, AllocationIndex>> traceIndexToAllocationIndex;

    /// find and return the index into the @c allocations vector for the given trace index.
    /// if the trace index wasn't mapped before, an empty Allocation will be added
    /// and its index returned.
    AllocationIndex mapToAllocationIndex(const TraceIndex traceIndex);

    const InstructionPointer& findIp(const IpIndex ipIndex) const;

    TraceNode findTrace(const TraceIndex traceIndex) const;

    bool isStopIndex(const StringIndex index) const;

    // indices of functions that should stop the backtrace, e.g. main or static
    // initialization
    std::vector<StringIndex> stopIndices;
    std::vector<InstructionPointer> instructionPointers;
    std::vector<TraceNode> traces;
    std::vector<std::string> strings;
    std::vector<IpIndex> opNewIpIndices;

    std::vector<AllocationInfo> allocationInfos;

    struct ParsingState
    {
        int64_t fileSize = 0; // bytes
        int64_t readCompressedByte = 0;
        int64_t readUncompressedByte = 0;
        int64_t timestamp = 0; // ms
        ParsePass pass = ParsePass::FirstPass;
        bool reparsing = false;
    };

    ParsingState parsingState;

    void applyLeakSuppressions();
    std::vector<Suppression> suppressions;
    int64_t totalLeakedSuppressed = 0;
};

#endif // ACCUMULATEDTRACEDATA_H
