/*
    SPDX-FileCopyrightText: 2015-2020 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "parser.h"

#include <KLocalizedString>
#include <ThreadWeaver/ThreadWeaver>

#include <QDebug>
#include <QElapsedTimer>
#include <QThread>

#include "analyze/accumulatedtracedata.h"

#include <future>
#include <tuple>
#include <utility>
#include <vector>

#define TSL_NO_EXCEPTIONS 1
#include <tsl/robin_map.h>
#include <tsl/robin_set.h>

#include <boost/functional/hash/hash.hpp>

namespace std {
template <>
struct hash<std::pair<Symbol, Symbol>>
{
    std::size_t operator()(const std::pair<Symbol, Symbol>& pair) const
    {
        return boost::hash_value(std::tie(pair.first.functionId.index, pair.first.moduleId.index,
                                          pair.second.functionId.index, pair.second.moduleId.index));
    }
};
}

using namespace std;

namespace {
Symbol symbol(const Frame& frame, ModuleIndex moduleIndex)
{
    return {frame.functionIndex, moduleIndex};
}

Symbol symbol(const InstructionPointer& ip)
{
    return symbol(ip.frame, ip.moduleIndex);
}

struct Location
{
    Symbol symbol;
    FileLine fileLine;
};
Location frameLocation(const Frame& frame, ModuleIndex moduleIndex)
{
    return {symbol(frame, moduleIndex), {frame.fileIndex, frame.line}};
}

Location location(const InstructionPointer& ip)
{
    return frameLocation(ip.frame, ip.moduleIndex);
}

struct ChartMergeData
{
    IpIndex ip;
    qint64 consumed;
    qint64 allocations;
    qint64 temporary;
    bool operator<(const IpIndex rhs) const
    {
        return ip < rhs;
    }
};

const uint64_t MAX_CHART_DATAPOINTS = 500; // TODO: make this configurable via the GUI

QVector<Suppression> toQt(const std::vector<Suppression>& suppressions)
{
    QVector<Suppression> ret(suppressions.size());
    std::copy(suppressions.begin(), suppressions.end(), ret.begin());
    return ret;
}
}

struct ParserData final : public AccumulatedTraceData
{
    using TimestampCallback = std::function<void(const ParserData& data)>;
    ParserData(TimestampCallback timestampCallback)
        : timestampCallback(std::move(timestampCallback))
    {
    }

    void prepareBuildCharts(const std::shared_ptr<const ResultData>& resultData)
    {
        if (diffMode) {
            return;
        }
        consumedChartData.resultData = resultData;
        consumedChartData.rows.reserve(MAX_CHART_DATAPOINTS);
        allocationsChartData.resultData = resultData;
        allocationsChartData.rows.reserve(MAX_CHART_DATAPOINTS);
        temporaryChartData.resultData = resultData;
        temporaryChartData.rows.reserve(MAX_CHART_DATAPOINTS);
        // start off with null data at the origin
        lastTimeStamp = filterParameters.minTime;
        ChartRows origin;
        origin.timeStamp = lastTimeStamp;
        consumedChartData.rows.push_back(origin);
        allocationsChartData.rows.push_back(origin);
        temporaryChartData.rows.push_back(origin);
        // index 0 indicates the total row
        consumedChartData.labels[0] = {};
        allocationsChartData.labels[0] = {};
        temporaryChartData.labels[0] = {};

        buildCharts = true;
        maxConsumedSinceLastTimeStamp = 0;
        vector<ChartMergeData> merged;
        merged.reserve(instructionPointers.size());
        // merge the allocation cost by instruction pointer
        // TODO: traverse the merged call stack up until the first fork
        for (const auto& alloc : allocations) {
            const auto ip = findTrace(alloc.traceIndex).ipIndex;
            auto it = lower_bound(merged.begin(), merged.end(), ip);
            if (it == merged.end() || it->ip != ip) {
                it = merged.insert(it, {ip, 0, 0, 0});
            }
            it->consumed += alloc.peak; // we want to track the top peaks in the chart
            it->allocations += alloc.allocations;
            it->temporary += alloc.temporary;
        }
        // find the top hot spots for the individual data members and remember their
        // IP and store the label
        tsl::robin_map<IpIndex, LabelIds> ipToLabelIds;
        auto findTopChartEntries = [&](qint64 ChartMergeData::*member, int LabelIds::*label, ChartData* data) {
            sort(merged.begin(), merged.end(), [=](const ChartMergeData& left, const ChartMergeData& right) {
                return std::abs(left.*member) > std::abs(right.*member);
            });
            for (size_t i = 0; i < min(size_t(ChartRows::MAX_NUM_COST - 2), merged.size()); ++i) {
                const auto& alloc = merged[i];
                if (!(alloc.*member)) {
                    break;
                }
                (ipToLabelIds[alloc.ip].*label) = i + 1;
                data->labels[i + 1] = symbol(findIp(alloc.ip));
                Q_ASSERT(data->labels.size() < ChartRows::MAX_NUM_COST);
            }
        };
        ipToLabelIds.reserve(3 * ChartRows::MAX_NUM_COST);
        findTopChartEntries(&ChartMergeData::consumed, &LabelIds::consumed, &consumedChartData);
        findTopChartEntries(&ChartMergeData::allocations, &LabelIds::allocations, &allocationsChartData);
        findTopChartEntries(&ChartMergeData::temporary, &LabelIds::temporary, &temporaryChartData);

        // now iterate the allocations once to build the list of allocations
        // we need to look at when we are building the charts in handleTimeStamp
        // instead of doing this lookup every time we are handling a time stamp
        for (uint32_t i = 0, c = allocations.size(); i < c; ++i) {
            const auto ip = findTrace(allocations[i].traceIndex).ipIndex;
            auto it = ipToLabelIds.find(ip);
            if (it == ipToLabelIds.end())
                continue;
            auto ids = it->second;
            ids.allocationIndex.index = i;
            labelIds.push_back(ids);
        }
    }

    void handleTimeStamp(int64_t /*oldStamp*/, int64_t newStamp, bool isFinalTimeStamp, ParsePass pass) override
    {
        if (timestampCallback) {
            timestampCallback(*this);
        }
        if (pass == ParsePass::FirstPass) {
            return;
        }
        if (!buildCharts || diffMode) {
            return;
        }
        maxConsumedSinceLastTimeStamp = max(maxConsumedSinceLastTimeStamp, totalCost.leaked);
        const auto timeSpan = (filterParameters.maxTime - filterParameters.minTime);
        const int64_t diffBetweenTimeStamps = timeSpan / MAX_CHART_DATAPOINTS;
        if (!isFinalTimeStamp && (newStamp - lastTimeStamp) < diffBetweenTimeStamps) {
            return;
        }
        const auto nowConsumed = maxConsumedSinceLastTimeStamp;
        maxConsumedSinceLastTimeStamp = 0;
        lastTimeStamp = newStamp;

        // create the rows
        auto createRow = [newStamp](int64_t totalCost) {
            ChartRows row;
            row.timeStamp = newStamp;
            row.cost[0] = totalCost;
            return row;
        };
        auto consumed = createRow(nowConsumed);
        auto allocs = createRow(totalCost.allocations);
        auto temporary = createRow(totalCost.temporary);

        // if the cost is non-zero and the ip corresponds to a hotspot function
        // selected in the labels, we add the cost to the rows column
        auto addDataToRow = [](int64_t cost, int labelId, ChartRows* rows) {
            if (!cost || labelId == -1) {
                return;
            }
            rows->cost[labelId] += cost;
        };
        for (const auto& ids : labelIds) {
            const auto alloc = allocations[ids.allocationIndex.index];
            addDataToRow(alloc.leaked, ids.consumed, &consumed);
            addDataToRow(alloc.allocations, ids.allocations, &allocs);
            addDataToRow(alloc.temporary, ids.temporary, &temporary);
        }
        // add the rows for this time stamp
        consumedChartData.rows << consumed;
        allocationsChartData.rows << allocs;
        temporaryChartData.rows << temporary;
    }

    void handleAllocation(const AllocationInfo& info, const AllocationInfoIndex index) override
    {
        maxConsumedSinceLastTimeStamp = max(maxConsumedSinceLastTimeStamp, totalCost.leaked);

        if (!diffMode) {
            if (index.index == allocationInfoCounter.size()) {
                allocationInfoCounter.push_back({info, 1});
            } else {
                ++allocationInfoCounter[index.index].allocations;
            }
        }
    }

    void handleDebuggee(const char* command) override
    {
        debuggee = command;
    }

    void clearForReparse()
    {
        // data moved to size histogram
        if (!diffMode) {
            // we have to reset the allocation count
            for (auto& info : allocationInfoCounter)
                info.allocations = 0;
            // and restore the order to allow fast direct access
            std::sort(allocationInfoCounter.begin(), allocationInfoCounter.end(),
                      [](const ParserData::CountedAllocationInfo& lhs, const ParserData::CountedAllocationInfo& rhs) {
                          return lhs.info.allocationIndex < rhs.info.allocationIndex;
                      });
        }

        // data moved to chart models
        consumedChartData = {};
        allocationsChartData = {};
        temporaryChartData = {};
        labelIds.clear();
        maxConsumedSinceLastTimeStamp = 0;
        lastTimeStamp = 0;
        buildCharts = false;
    }

    string debuggee;

    struct CountedAllocationInfo
    {
        AllocationInfo info;
        int64_t allocations;
        bool operator<(const CountedAllocationInfo& rhs) const
        {
            return tie(info.size, allocations) < tie(rhs.info.size, rhs.allocations);
        }
    };
    /// counts how often a given allocation info is encountered based on its index
    /// used to build the size histogram
    /// this is disabled when we are diffing two files
    vector<CountedAllocationInfo> allocationInfoCounter;

    ChartData consumedChartData;
    ChartData allocationsChartData;
    ChartData temporaryChartData;
    // here we store the indices into ChartRows::cost for those IpIndices that
    // are within the top hotspots. This way, we can do one hash lookup in the
    // handleTimeStamp function instead of three when we'd store this data
    // in a per-ChartData hash.
    struct LabelIds
    {
        AllocationIndex allocationIndex;
        int consumed = -1;
        int allocations = -1;
        int temporary = -1;
    };
    vector<LabelIds> labelIds;
    int64_t maxConsumedSinceLastTimeStamp = 0;
    int64_t lastTimeStamp = 0;

    bool buildCharts = false;
    bool diffMode = false;

    TimestampCallback timestampCallback;
    QElapsedTimer parseTimer;

    QVector<QString> qtStrings;
};

namespace {
void setParents(QVector<RowData>& children, const RowData* parent)
{
    children.squeeze();
    for (auto& row : children) {
        row.parent = parent;
        setParents(row.children, &row);
    }
}

void addCallerCalleeEvent(const Location& location, const AllocationData& cost, tsl::robin_set<Symbol>* recursionGuard,
                          CallerCalleeResults* callerCalleeResult)
{
    const auto isLeaf = recursionGuard->empty();
    if (!recursionGuard->insert(location.symbol).second) {
        return;
    }

    auto& entry = callerCalleeResult->entries[location.symbol];
    auto& locationCost = entry.sourceMap[location.fileLine];

    locationCost.inclusiveCost += cost;
    if (isLeaf) {
        // increment self cost for leaf
        locationCost.selfCost += cost;
    }
}

std::pair<TreeData, CallerCalleeResults> mergeAllocations(Parser* parser, const ParserData& data,
                                                          std::shared_ptr<const ResultData> resultData)
{
    CallerCalleeResults callerCalleeResults;
    TreeData topRows;
    tsl::robin_set<TraceIndex> traceRecursionGuard;
    traceRecursionGuard.reserve(128);
    tsl::robin_set<Symbol> symbolRecursionGuard;
    symbolRecursionGuard.reserve(128);
    auto addRow = [&symbolRecursionGuard, &callerCalleeResults](QVector<RowData>* rows, const Location& location,
                                                                const Allocation& cost) -> QVector<RowData>* {
        auto it = lower_bound(rows->begin(), rows->end(), location.symbol);
        if (it != rows->end() && it->symbol == location.symbol) {
            it->cost += cost;
        } else {
            it = rows->insert(it, {cost, location.symbol, nullptr, {}});
        }
        addCallerCalleeEvent(location, cost, &symbolRecursionGuard, &callerCalleeResults);
        return &it->children;
    };
    const auto allocationCount = data.allocations.size();
    const auto onePercent = std::max<size_t>(1, allocationCount / 100);
    auto progress = 0;
    // merge allocations, leave parent pointers invalid (their location may change)
    for (const auto& allocation : data.allocations) {
        auto traceIndex = allocation.traceIndex;
        auto rows = &topRows.rows;
        traceRecursionGuard.clear();
        traceRecursionGuard.insert(traceIndex);
        symbolRecursionGuard.clear();
        bool first = true;
        while (traceIndex || first) {
            first = false;
            const auto& trace = data.findTrace(traceIndex);
            const auto& ip = data.findIp(trace.ipIndex);
            rows = addRow(rows, location(ip), allocation);
            for (const auto& inlined : ip.inlined) {
                const auto& inlinedLocation = frameLocation(inlined, ip.moduleIndex);
                rows = addRow(rows, inlinedLocation, allocation);
            }
            if (data.isStopIndex(ip.frame.functionIndex)) {
                break;
            }
            traceIndex = trace.parentIndex;
            if (!traceRecursionGuard.insert(traceIndex).second) {
                qWarning() << "Trace recursion detected - corrupt data file?";
                break;
            }
        }
        ++progress;
        if ((progress % onePercent) == 0) {
            const int percent = progress * 100 / allocationCount;
            emit parser->progressMessageAvailable(i18n("merging allocations... %1%", percent));
        }
    }
    // now set the parents, the data is constant from here on
    setParents(topRows.rows, nullptr);

    topRows.resultData = std::move(resultData);
    return {topRows, callerCalleeResults};
}

RowData* findBySymbol(Symbol symbol, QVector<RowData>* data)
{
    auto it = std::find_if(data->begin(), data->end(), [symbol](const RowData& row) { return row.symbol == symbol; });
    return it == data->end() ? nullptr : &(*it);
}

AllocationData buildTopDown(const QVector<RowData>& bottomUpData, QVector<RowData>* topDownData)
{
    AllocationData totalCost;
    for (const auto& row : bottomUpData) {
        // recurse and find the cost attributed to children
        const auto childCost = buildTopDown(row.children, topDownData);
        if (childCost != row.cost) {
            // this row is (partially) a leaf
            const auto cost = row.cost - childCost;

            // bubble up the parent chain to build a top-down tree
            auto node = &row;
            auto stack = topDownData;
            while (node) {
                auto data = findBySymbol(node->symbol, stack);
                if (!data) {
                    // create an empty top-down item for this bottom-up node
                    *stack << RowData {{}, node->symbol, nullptr, {}};
                    data = &stack->back();
                }
                // always use the leaf node's cost and propagate that one up the chain
                // otherwise we'd count the cost of some nodes multiple times
                data->cost += cost;
                stack = &data->children;
                node = node->parent;
            }
        }
        totalCost += row.cost;
    }
    return totalCost;
}

TreeData toTopDownData(const TreeData& bottomUpData)
{
    TreeData topRows;
    topRows.resultData = bottomUpData.resultData;
    buildTopDown(bottomUpData.rows, &topRows.rows);
    // now set the parents, the data is constant from here on
    setParents(topRows.rows, nullptr);
    return topRows;
}

struct ReusableGuardBuffer
{
    ReusableGuardBuffer()
    {
        recursionGuard.reserve(128);
        callerCalleeRecursionGuard.reserve(128);
    }

    void reset()
    {
        recursionGuard.clear();
        callerCalleeRecursionGuard.clear();
    }

    tsl::robin_set<Symbol> recursionGuard;
    tsl::robin_set<std::pair<Symbol, Symbol>> callerCalleeRecursionGuard;
};

AllocationData buildCallerCallee(const QVector<RowData>& bottomUpData, CallerCalleeResults* callerCalleeResults,
                                 ReusableGuardBuffer* guardBuffer)
{
    AllocationData totalCost;
    for (const auto& row : bottomUpData) {
        // recurse to find a leaf
        const auto childCost = buildCallerCallee(row.children, callerCalleeResults, guardBuffer);
        if (childCost != row.cost) {
            // this row is (partially) a leaf
            const auto cost = row.cost - childCost;

            // leaf node found, bubble up the parent chain to add cost for all frames
            // to the caller/callee data. this is done top-down since we must not count
            // symbols more than once in the caller-callee data
            guardBuffer->reset();
            auto& recursionGuard = guardBuffer->recursionGuard;
            auto& callerCalleeRecursionGuard = guardBuffer->callerCalleeRecursionGuard;

            auto node = &row;

            Symbol lastSymbol;
            CallerCalleeEntry* lastEntry = nullptr;

            while (node) {
                const auto symbol = node->symbol;
                // aggregate caller-callee data
                auto& entry = callerCalleeResults->entries[symbol];
                if (recursionGuard.insert(symbol).second) {
                    // only increment inclusive cost once for a given stack
                    entry.inclusiveCost += cost;
                }
                if (!node->parent) {
                    // always increment the self cost
                    entry.selfCost += cost;
                }
                // add current entry as callee to last entry
                // and last entry as caller to current entry
                if (lastEntry) {
                    if (callerCalleeRecursionGuard.insert({symbol, lastSymbol}).second) {
                        lastEntry->callees[symbol] += cost;
                        entry.callers[lastSymbol] += cost;
                    }
                }

                node = node->parent;
                lastSymbol = symbol;
                lastEntry = &entry;
            }
        }
        totalCost += row.cost;
    }
    return totalCost;
}

CallerCalleeResults toCallerCalleeData(const TreeData& bottomUpData, const CallerCalleeResults& results, bool diffMode)
{
    // copy the source map and continue from there
    auto callerCalleeResults = results;
    ReusableGuardBuffer guardBuffer;
    buildCallerCallee(bottomUpData.rows, &callerCalleeResults, &guardBuffer);

    if (diffMode) {
        // remove rows without cost
        for (auto it = callerCalleeResults.entries.begin(); it != callerCalleeResults.entries.end();) {
            if (it->inclusiveCost == AllocationData() && it->selfCost == AllocationData()) {
                it = callerCalleeResults.entries.erase(it);
            } else {
                ++it;
            }
        }
    }

    callerCalleeResults.resultData = bottomUpData.resultData;
    return callerCalleeResults;
}

struct MergedHistogramColumnData
{
    Symbol symbol;
    int64_t allocations;
    int64_t totalAllocated;
    bool operator<(const Symbol& rhs) const
    {
        return symbol < rhs;
    }
};

HistogramData buildSizeHistogram(ParserData& data, std::shared_ptr<const ResultData> resultData)
{
    HistogramData ret;
    Q_ASSERT(!data.diffMode || data.allocationInfoCounter.empty());
    if (data.allocationInfoCounter.empty()) {
        return ret;
    }
    sort(data.allocationInfoCounter.begin(), data.allocationInfoCounter.end());
    const auto totalLabel = i18n("total");
    HistogramRow row;
    const pair<uint64_t, QString> buckets[] = {{8, i18n("0B to 8B")},
                                               {16, i18n("9B to 16B")},
                                               {32, i18n("17B to 32B")},
                                               {64, i18n("33B to 64B")},
                                               {128, i18n("65B to 128B")},
                                               {256, i18n("129B to 256B")},
                                               {512, i18n("257B to 512B")},
                                               {1024, i18n("512B to 1KB")},
                                               {numeric_limits<uint64_t>::max(), i18n("more than 1KB")}};
    uint bucketIndex = 0;
    row.size = buckets[bucketIndex].first;
    row.sizeLabel = buckets[bucketIndex].second;
    vector<MergedHistogramColumnData> columnData;
    columnData.reserve(128);
    auto insertColumns = [&]() {
        sort(columnData.begin(), columnData.end(),
             [](const MergedHistogramColumnData& lhs, const MergedHistogramColumnData& rhs) {
                 return std::tie(lhs.allocations, lhs.totalAllocated) > std::tie(rhs.allocations, rhs.totalAllocated);
             });
        // -1 to account for total row
        for (size_t i = 0; i < min(columnData.size(), size_t(HistogramRow::NUM_COLUMNS - 1)); ++i) {
            const auto& column = columnData[i];
            row.columns[i + 1] = {column.allocations, column.totalAllocated, column.symbol};
        }
    };
    for (const auto& info : data.allocationInfoCounter) {
        if (info.info.size > row.size) {
            insertColumns();
            columnData.clear();
            ret.rows << row;
            ++bucketIndex;
            row.size = buckets[bucketIndex].first;
            row.sizeLabel = buckets[bucketIndex].second;
            row.columns[0] = {info.allocations, static_cast<qint64>(info.info.size * info.allocations), {}};
        } else {
            auto& column = row.columns[0];
            column.allocations += info.allocations;
            column.totalAllocated += info.info.size * info.allocations;
        }
        const auto& allocation = data.allocations[info.info.allocationIndex.index];
        const auto& ipIndex = data.findTrace(allocation.traceIndex).ipIndex;
        const auto& ip = data.findIp(ipIndex);
        const auto& sym = symbol(ip);
        auto it = lower_bound(columnData.begin(), columnData.end(), sym);
        if (it == columnData.end() || it->symbol != sym) {
            columnData.insert(it, {sym, info.allocations, static_cast<qint64>(info.info.size * info.allocations)});
        } else {
            it->allocations += info.allocations;
            it->totalAllocated += static_cast<qint64>(info.info.size * info.allocations);
        }
    }
    insertColumns();
    ret.rows << row;
    ret.resultData = std::move(resultData);
    return ret;
}
}

Parser::Parser(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<SummaryData>();
}

Parser::~Parser() = default;

bool Parser::isFiltered() const
{
    if (!m_data)
        return false;
    return m_data->filterParameters.isFilteredByTime(m_data->totalTime);
}

void Parser::parse(const QString& path, const QString& diffBase, const FilterParameters& filterParameters,
                   StopAfter stopAfter)
{
    parseImpl(path, diffBase, filterParameters, stopAfter);
}

void Parser::parseImpl(const QString& path, const QString& diffBase, const FilterParameters& filterParameters,
                       StopAfter stopAfter)
{
    auto oldData = std::move(m_data);
    using namespace ThreadWeaver;
    stream() << make_job([this, oldData, path, diffBase, filterParameters, stopAfter]() {
        const auto isReparsing = (path == m_path && oldData && diffBase.isEmpty());
        auto parsingMsg = isReparsing ? i18n("reparsing data") : i18n("parsing data");

        auto updateProgress = [this, parsingMsg, lastPassCompletion = 0.f](const ParserData& data) mutable {
            auto passCompletion = 1.0 * data.parsingState.readCompressedByte / data.parsingState.fileSize;
            if (std::abs(lastPassCompletion - passCompletion) < 0.001) {
                // don't spam the progress bar
                return;
            }

            lastPassCompletion = passCompletion;
            const auto numPasses = data.diffMode ? 2 : 3;
            auto totalCompletion = (data.parsingState.pass + passCompletion) / numPasses;
            auto spentTime_ms = data.parseTimer.elapsed();
            auto totalRemainingTime_ms = (spentTime_ms / totalCompletion) * (1.0 - totalCompletion);
            auto message = i18n("%1 pass: %2/%3  spent: %4  remaining: %5", parsingMsg, data.parsingState.pass + 1,
                                numPasses, Util::formatTime(spentTime_ms), Util::formatTime(totalRemainingTime_ms));

            emit progressMessageAvailable(message);
            emit progress(1000 * totalCompletion); // range is set as 0 to 1000 for fractional % bar display
        };

        const auto stdPath = path.toStdString();
        auto data = isReparsing ? oldData : make_shared<ParserData>(updateProgress);
        data->filterParameters = filterParameters;

        emit progressMessageAvailable(parsingMsg);
        data->parseTimer.start();

        data->diffMode = !diffBase.isEmpty();

        if (data->diffMode) {
            ParserData diffData(nullptr); // currently we don't track the progress of diff parsing
            diffData.diffMode = true;
            auto readBase = async(launch::async, [&diffData, diffBase, isReparsing]() {
                return diffData.read(diffBase.toStdString(), isReparsing);
            });
            if (!data->read(stdPath, isReparsing)) {
                emit failedToOpen(path);
                return;
            }
            if (!readBase.get()) {
                emit failedToOpen(diffBase);
                return;
            }
            data->diff(diffData);
        } else {
            if (!data->read(stdPath, isReparsing)) {
                emit failedToOpen(path);
                return;
            }
        }

        if (!isReparsing) {
            data->qtStrings.resize(data->strings.size());
            std::transform(data->strings.begin(), data->strings.end(), data->qtStrings.begin(),
                           [](const std::string& string) { return QString::fromStdString(string); });
        }

        data->applyLeakSuppressions();

        const auto resultData = std::make_shared<const ResultData>(data->totalCost, data->qtStrings);

        emit summaryAvailable({QString::fromStdString(data->debuggee), data->totalCost, data->totalTime,
                               data->filterParameters, data->peakTime, data->peakRSS * data->systemInfo.pageSize,
                               data->systemInfo.pages * data->systemInfo.pageSize, data->fromAttached,
                               data->totalLeakedSuppressed, toQt(data->suppressions)});

        if (stopAfter == StopAfter::Summary) {
            emit finished();
            return;
        }

        emit progressMessageAvailable(i18n("merging allocations..."));
        // merge allocations before modifying the data again
        const auto mergedAllocations = mergeAllocations(this, *data, resultData);
        emit bottomUpDataAvailable(mergedAllocations.first);

        if (stopAfter == StopAfter::BottomUp) {
            emit finished();
            return;
        }

        // calculate the size histogram when we are not diffing
        if (!data->diffMode) {
            emit progressMessageAvailable(i18n("building size histogram..."));
            const auto sizeHistogram = buildSizeHistogram(*data, resultData);
            emit sizeHistogramDataAvailable(sizeHistogram);

            if (stopAfter == StopAfter::SizeHistogram) {
                emit finished();
                return;
            }
        }

        // now data can be modified again for the chart data evaluation

        emit progress(0);

        emit progressMessageAvailable(i18n("building charts..."));
        auto parallel = new Collection;
        *parallel << make_job([this, mergedAllocations, resultData]() {
            const auto topDownData = toTopDownData(mergedAllocations.first);
            emit topDownDataAvailable(topDownData);
        }) << make_job([this, mergedAllocations, diffMode = data->diffMode]() {
            emit callerCalleeDataAvailable(
                toCallerCalleeData(mergedAllocations.first, mergedAllocations.second, diffMode));
        });
        if (!data->diffMode && stopAfter != StopAfter::TopDownAndCallerCallee) {
            // only build charts when we are not diffing
            *parallel << make_job([this, data, stdPath, isReparsing, resultData]() {
                // this mutates data, and thus anything running in parallel must
                // not access data
                data->prepareBuildCharts(resultData);
                data->read(stdPath, AccumulatedTraceData::ThirdPass, isReparsing);
                emit consumedChartDataAvailable(data->consumedChartData);
                emit allocationsChartDataAvailable(data->allocationsChartData);
                emit temporaryChartDataAvailable(data->temporaryChartData);
            });
        }

        emit progress(0);

        auto sequential = new Sequence;
        *sequential << parallel << make_job([this, data, path]() {
            QMetaObject::invokeMethod(this, [this, data, path]() {
                Q_ASSERT(QThread::currentThread() == thread());
                m_data = data;
                m_data->clearForReparse();
                m_path = path;
                emit finished();
            });
        });

        stream() << sequential;
    });
}

void Parser::reparse(const FilterParameters& parameters_)
{
    if (!m_data || m_data->diffMode)
        return;

    auto filterParameters = parameters_;
    filterParameters.minTime = std::max(int64_t(0), filterParameters.minTime);
    filterParameters.maxTime = std::min(m_data->totalTime, filterParameters.maxTime);

    parseImpl(m_path, {}, filterParameters, StopAfter::Finished);
}

#include "moc_parser.cpp"
