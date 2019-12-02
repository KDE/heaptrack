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

#include "parser.h"

#include <KLocalizedString>
#include <ThreadWeaver/ThreadWeaver>

#include <QDebug>

#include "analyze/accumulatedtracedata.h"

#include <future>
#include <tuple>
#include <vector>
#include <unordered_map>

using namespace std;

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
namespace std {
template <>
struct hash<QString>
{
    std::size_t operator()(const QString &v) const noexcept
    {
        return qHash(v);
    }
};
}
#endif

// Only use this hash when filling in the cache
struct CacheSymbolHash
{
    std::size_t operator()(const Symbol &symbol) const noexcept
    {
        size_t seed = 0;
        boost::hash_combine(seed, std::hash<QString>{}(symbol.symbol));
        boost::hash_combine(seed, std::hash<QString>{}(symbol.binary));
        boost::hash_combine(seed, std::hash<QString>{}(symbol.path));
        return seed;
    }
};

namespace {

struct Location
{
    Symbol symbol;
    FileLine fileLine;
};

struct StringCache
{
    QString func(const Frame& frame) const
    {
        if (frame.functionIndex) {
            // TODO: support removal of template arguments
            return stringify(frame.functionIndex);
        } else {
            return unresolvedFunctionName();
        }
    }

    QString file(const Frame& frame) const
    {
        return stringify(frame.fileIndex);
    }

    QString stringify(const StringIndex index) const
    {
        if (!index || index.index > m_strings.size()) {
            return {};
        } else {
            return m_strings.at(index.index - 1);
        }
    }

    Location location(const InstructionPointer& ip) const
    {
        return frameLocation(ip.frame, ip.moduleIndex);
    }

    Location frameLocation(const Frame& frame, const ModuleIndex& moduleIndex) const
    {
        const auto module = stringify(moduleIndex);
        auto binaryIt = m_pathToBinaries.find(module);
        if (binaryIt == m_pathToBinaries.end()) {
            binaryIt = m_pathToBinaries.insert(module, Util::basename(module));
        }
        const SymbolId id = ++m_nextSymbolId;
        auto symbol = Symbol{func(frame), *binaryIt, module, id};
        // Insert symbol into the hash, or use existing one (in which case the new ID won't be used)
        const auto it = m_symbols.emplace(std::make_pair(std::move(symbol), id)).first;
        return {it->first, {file(frame), frame.line}};
    }

    void update(const vector<string>& strings)
    {
        transform(strings.begin() + m_strings.size(), strings.end(), back_inserter(m_strings),
                  [](const string& str) { return QString::fromStdString(str); });
    }

    vector<QString> m_strings;
    // interned module basenames
    mutable QHash<QString, QString> m_pathToBinaries;
    // existing symbols
    mutable std::unordered_map<Symbol, SymbolId, CacheSymbolHash, Symbol::FullEqual> m_symbols;

    bool diffMode = false;

    mutable SymbolId m_nextSymbolId = 0;
};

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

struct ParserData final : public AccumulatedTraceData
{
    ParserData()
    {
    }

    void updateStringCache()
    {
        stringCache.update(strings);
    }

    void prepareBuildCharts()
    {
        if (stringCache.diffMode) {
            return;
        }
        consumedChartData.rows.reserve(MAX_CHART_DATAPOINTS);
        allocationsChartData.rows.reserve(MAX_CHART_DATAPOINTS);
        temporaryChartData.rows.reserve(MAX_CHART_DATAPOINTS);
        // start off with null data at the origin
        consumedChartData.rows.push_back({});
        allocationsChartData.rows.push_back({});
        temporaryChartData.rows.push_back({});
        // index 0 indicates the total row
        consumedChartData.labels[0] = i18n("total");
        allocationsChartData.labels[0] = i18n("total");
        temporaryChartData.labels[0] = i18n("total");

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
        auto findTopChartEntries = [&](qint64 ChartMergeData::*member, int LabelIds::*label, ChartData* data) {
            sort(merged.begin(), merged.end(),
                 [=](const ChartMergeData& left, const ChartMergeData& right) { return left.*member > right.*member; });
            for (size_t i = 0; i < min(size_t(ChartRows::MAX_NUM_COST - 1), merged.size()); ++i) {
                const auto& alloc = merged[i];
                if (!(alloc.*member)) {
                    break;
                }
                const auto ip = alloc.ip;
                (labelIds[ip].*label) = i + 1;
                const auto function = stringCache.func(findIp(ip).frame);
                data->labels[i + 1] = function;
            }
        };
        findTopChartEntries(&ChartMergeData::consumed, &LabelIds::consumed, &consumedChartData);
        findTopChartEntries(&ChartMergeData::allocations, &LabelIds::allocations, &allocationsChartData);
        findTopChartEntries(&ChartMergeData::temporary, &LabelIds::temporary, &temporaryChartData);
    }

    void handleTimeStamp(int64_t /*oldStamp*/, int64_t newStamp) override
    {
        if (!buildCharts || stringCache.diffMode) {
            return;
        }
        maxConsumedSinceLastTimeStamp = max(maxConsumedSinceLastTimeStamp, totalCost.leaked);
        const int64_t diffBetweenTimeStamps = totalTime / MAX_CHART_DATAPOINTS;
        if (newStamp != totalTime && newStamp - lastTimeStamp < diffBetweenTimeStamps) {
            return;
        }
        const auto nowConsumed = maxConsumedSinceLastTimeStamp;
        maxConsumedSinceLastTimeStamp = 0;
        lastTimeStamp = newStamp;

        // create the rows
        auto createRow = [](int64_t timeStamp, int64_t totalCost) {
            ChartRows row;
            row.timeStamp = timeStamp;
            row.cost[0] = totalCost;
            return row;
        };
        auto consumed = createRow(newStamp, nowConsumed);
        auto allocs = createRow(newStamp, totalCost.allocations);
        auto temporary = createRow(newStamp, totalCost.temporary);

        // if the cost is non-zero and the ip corresponds to a hotspot function
        // selected in the labels,
        // we add the cost to the rows column
        auto addDataToRow = [](int64_t cost, int labelId, ChartRows* rows) {
            if (!cost || labelId == -1) {
                return;
            }
            rows->cost[labelId] += cost;
        };
        for (const auto& alloc : allocations) {
            const auto ip = findTrace(alloc.traceIndex).ipIndex;
            auto it = labelIds.constFind(ip);
            if (it == labelIds.constEnd()) {
                continue;
            }
            const auto& labelIds = *it;
            addDataToRow(alloc.leaked, labelIds.consumed, &consumed);
            addDataToRow(alloc.allocations, labelIds.allocations, &allocs);
            addDataToRow(alloc.temporary, labelIds.temporary, &temporary);
        }
        // add the rows for this time stamp
        consumedChartData.rows << consumed;
        allocationsChartData.rows << allocs;
        temporaryChartData.rows << temporary;
    }

    void handleAllocation(const AllocationInfo& info, const AllocationInfoIndex index) override
    {
        maxConsumedSinceLastTimeStamp = max(maxConsumedSinceLastTimeStamp, totalCost.leaked);

        if (index.index == allocationInfoCounter.size()) {
            allocationInfoCounter.push_back({info, 1});
        } else {
            ++allocationInfoCounter[index.index].allocations;
        }
    }

    void handleDebuggee(const char* command) override
    {
        debuggee = command;
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
        int consumed = -1;
        int allocations = -1;
        int temporary = -1;
    };
    QHash<IpIndex, LabelIds> labelIds;
    int64_t maxConsumedSinceLastTimeStamp = 0;
    int64_t lastTimeStamp = 0;

    StringCache stringCache;

    bool buildCharts = false;
};

void setParents(QVector<RowData>& children, const RowData* parent)
{
    children.squeeze();
    for (auto& row : children) {
        row.parent = parent;
        setParents(row.children, &row);
    }
}

void addCallerCalleeEvent(const Location& location, const AllocationData& cost, QSet<Symbol>* recursionGuard,
                          CallerCalleeResults* callerCalleeResult)
{
    auto recursionIt = recursionGuard->find(location.symbol);
    if (recursionIt == recursionGuard->end()) {
        auto& entry = callerCalleeResult->entry(location.symbol);
        auto& locationCost = entry.source(location.fileLine);

        locationCost.inclusiveCost += cost;
        if (recursionGuard->isEmpty()) {
            // increment self cost for leaf
            locationCost.selfCost += cost;
        }
        recursionGuard->insert(location.symbol);
    }
}

std::pair<TreeData, CallerCalleeResults> mergeAllocations(Parser *parser, const ParserData& data)
{
    CallerCalleeResults callerCalleeResults;
    TreeData topRows;
    QSet<Symbol> symbolRecursionGuard;
    auto addRow = [&symbolRecursionGuard, &callerCalleeResults](TreeData* rows, const Location& location,
                                                                const Allocation& cost) -> TreeData* {
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
        auto rows = &topRows;
        unordered_set<uint32_t> recursionGuard;
        recursionGuard.insert(traceIndex.index);
        symbolRecursionGuard.clear();
        while (traceIndex) {
            const auto& trace = data.findTrace(traceIndex);
            const auto& ip = data.findIp(trace.ipIndex);
            auto location = data.stringCache.location(ip);
            rows = addRow(rows, location, allocation);
            for (const auto& inlined : ip.inlined) {
                auto inlinedLocation = data.stringCache.frameLocation(inlined, ip.moduleIndex);
                rows = addRow(rows, inlinedLocation, allocation);
            }
            if (data.isStopIndex(ip.frame.functionIndex)) {
                break;
            }
            traceIndex = trace.parentIndex;
            if (!recursionGuard.insert(traceIndex.index).second) {
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
    setParents(topRows, nullptr);

    return {topRows, callerCalleeResults};
}

RowData* findBySymbol(const RowData& row, QVector<RowData>* data)
{
    for (int i = 0; i < data->size(); ++i) {
        if (data->at(i).symbol == row.symbol) {
            return data->data() + i;
        }
    }
    return nullptr;
}

AllocationData buildTopDown(const TreeData& bottomUpData, TreeData* topDownData)
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
                auto data = findBySymbol(*node, stack);
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

QVector<RowData> toTopDownData(const QVector<RowData>& bottomUpData)
{
    QVector<RowData> topRows;
    buildTopDown(bottomUpData, &topRows);
    // now set the parents, the data is constant from here on
    setParents(topRows, nullptr);
    return topRows;
}

AllocationData buildCallerCallee(const TreeData& bottomUpData, CallerCalleeResults* callerCalleeResults)
{
    AllocationData totalCost;
    for (const auto& row : bottomUpData) {
        // recurse to find a leaf
        const auto childCost = buildCallerCallee(row.children, callerCalleeResults);
        if (childCost != row.cost) {
            // this row is (partially) a leaf
            const auto cost = row.cost - childCost;

            // leaf node found, bubble up the parent chain to add cost for all frames
            // to the caller/callee data. this is done top-down since we must not count
            // symbols more than once in the caller-callee data
            QSet<Symbol> recursionGuard;
            auto node = &row;

            QSet<QPair<Symbol, Symbol>> callerCalleeRecursionGuard;
            Symbol lastSymbol;
            CallerCalleeEntry* lastEntry = nullptr;

            while (node) {
                const auto& symbol = node->symbol;
                // aggregate caller-callee data
                auto& entry = callerCalleeResults->entry(symbol);
                if (!recursionGuard.contains(symbol)) {
                    // only increment inclusive cost once for a given stack
                    entry.inclusiveCost += cost;
                    recursionGuard.insert(symbol);
                }
                if (!node->parent) {
                    // always increment the self cost
                    entry.selfCost += cost;
                }
                // add current entry as callee to last entry
                // and last entry as caller to current entry
                if (lastEntry) {
                    const auto callerCalleePair = qMakePair(symbol, lastSymbol);
                    if (!callerCalleeRecursionGuard.contains(callerCalleePair)) {
                        lastEntry->callee(symbol) += cost;
                        entry.caller(lastSymbol) += cost;
                        callerCalleeRecursionGuard.insert(callerCalleePair);
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
    callerCalleeResults.totalCosts = buildCallerCallee(bottomUpData, &callerCalleeResults);

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

    return callerCalleeResults;
}

struct MergedHistogramColumnData
{
    Symbol symbol;
    int64_t allocations;
    bool operator<(const Symbol& rhs) const
    {
        return symbol < rhs;
    }
};

HistogramData buildSizeHistogram(ParserData& data)
{
    HistogramData ret;
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
                 return lhs.allocations > rhs.allocations;
             });
        // -1 to account for total row
        for (size_t i = 0; i < min(columnData.size(), size_t(HistogramRow::NUM_COLUMNS - 1)); ++i) {
            const auto& column = columnData[i];
            row.columns[i + 1] = {column.allocations, column.symbol};
        }
    };
    for (const auto& info : data.allocationInfoCounter) {
        if (info.info.size > row.size) {
            insertColumns();
            columnData.clear();
            ret << row;
            ++bucketIndex;
            row.size = buckets[bucketIndex].first;
            row.sizeLabel = buckets[bucketIndex].second;
            row.columns[0] = {info.allocations, {}};
        } else {
            row.columns[0].allocations += info.allocations;
        }
        const auto& allocation = data.allocations[info.info.allocationIndex.index];
        const auto ipIndex = data.findTrace(allocation.traceIndex).ipIndex;
        const auto ip = data.findIp(ipIndex);
        const auto location = data.stringCache.location(ip);
        auto it = lower_bound(columnData.begin(), columnData.end(), location.symbol);
        if (it == columnData.end() || it->symbol != location.symbol) {
            columnData.insert(it, {location.symbol, info.allocations});
        } else {
            it->allocations += info.allocations;
        }
    }
    insertColumns();
    ret << row;
    return ret;
}
}

Parser::Parser(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<SummaryData>();
}

Parser::~Parser() = default;

void Parser::parse(const QString& path, const QString& diffBase)
{
    using namespace ThreadWeaver;
    stream() << make_job([this, path, diffBase]() {
        const auto stdPath = path.toStdString();
        auto data = make_shared<ParserData>();
        emit progressMessageAvailable(i18n("parsing data..."));

        if (!diffBase.isEmpty()) {
            ParserData diffData;
            auto readBase =
                async(launch::async, [&diffData, diffBase]() { return diffData.read(diffBase.toStdString()); });
            if (!data->read(stdPath)) {
                emit failedToOpen(path);
                return;
            }
            if (!readBase.get()) {
                emit failedToOpen(diffBase);
                return;
            }
            data->diff(diffData);
            data->stringCache.diffMode = true;
        } else if (!data->read(stdPath)) {
            emit failedToOpen(path);
            return;
        }

        data->updateStringCache();

        emit summaryAvailable({QString::fromStdString(data->debuggee), data->totalCost, data->totalTime, data->peakTime,
                               data->peakRSS * data->systemInfo.pageSize,
                               data->systemInfo.pages * data->systemInfo.pageSize, data->fromAttached});

        emit progressMessageAvailable(i18n("merging allocations..."));
        // merge allocations before modifying the data again
        const auto mergedAllocations = mergeAllocations(this, *data);
        emit bottomUpDataAvailable(mergedAllocations.first);

        // also calculate the size histogram
        emit progressMessageAvailable(i18n("building size histogram..."));
        const auto sizeHistogram = buildSizeHistogram(*data);
        emit sizeHistogramDataAvailable(sizeHistogram);
        // now data can be modified again for the chart data evaluation

        const auto diffMode = data->stringCache.diffMode;
        emit progressMessageAvailable(i18n("building charts..."));
        auto parallel = new Collection;
        *parallel << make_job([this, mergedAllocations]() {
            const auto topDownData = toTopDownData(mergedAllocations.first);
            emit topDownDataAvailable(topDownData);
        }) << make_job([this, mergedAllocations, diffMode]() {
            emit callerCalleeDataAvailable(
                toCallerCalleeData(mergedAllocations.first, mergedAllocations.second, diffMode));
        });
        if (!data->stringCache.diffMode) {
            // only build charts when we are not diffing
            *parallel << make_job([this, data, stdPath]() {
                // this mutates data, and thus anything running in parallel must
                // not access data
                data->prepareBuildCharts();
                data->read(stdPath);
                emit consumedChartDataAvailable(data->consumedChartData);
                emit allocationsChartDataAvailable(data->allocationsChartData);
                emit temporaryChartDataAvailable(data->temporaryChartData);
            });
        }

        auto sequential = new Sequence;
        *sequential << parallel << make_job([this]() { emit finished(); });

        stream() << sequential;
    });
}
