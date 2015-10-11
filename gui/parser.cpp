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

#include "parser.h"

#include <ThreadWeaver/ThreadWeaver>
#include <KFormat>
#include <KLocalizedString>

#include <QTextStream>
#include <QDebug>

#include "../accumulatedtracedata.h"
#include "flamegraph.h"

#include <vector>

using namespace std;

namespace {

// TODO: use QString directly
struct StringCache
{
    StringCache()
    {
        m_ipAddresses.reserve(16384);
    }

    QString func(const InstructionPointer& ip) const
    {
        if (ip.functionIndex) {
            // TODO: support removal of template arguments
            return stringify(ip.functionIndex);
        } else {
            auto& ipAddr = m_ipAddresses[ip.instructionPointer];
            if (ipAddr.isEmpty()) {
                ipAddr = QLatin1String("0x") + QString::number(ip.instructionPointer, 16);
            }
            return ipAddr;
        }
    }

    QString file(const InstructionPointer& ip) const
    {
        if (ip.fileIndex) {
            return stringify(ip.fileIndex);
        } else {
            return {};
        }
    }

    QString module(const InstructionPointer& ip) const
    {
        return stringify(ip.moduleIndex);
    }

    QString stringify(const StringIndex index) const
    {
        if (!index || index.index > m_strings.size()) {
            return {};
        } else {
            return m_strings.at(index.index - 1);
        }
    }

    LocationData location(const InstructionPointer& ip) const
    {
        return {func(ip), file(ip), module(ip), ip.line};
    }

    void update(const vector<string>& strings)
    {
        transform(strings.begin() + m_strings.size(), strings.end(),
                  back_inserter(m_strings), [] (const string& str) { return QString::fromStdString(str); });
    }

    vector<QString> m_strings;
    mutable QHash<uint64_t, QString> m_ipAddresses;
};

struct ChartMergeData
{
    IpIndex ip;
    quint64 consumed;
    quint64 allocations;
    quint64 allocated;
    bool operator<(const IpIndex rhs) const
    {
        return ip < rhs;
    }
};

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
        // start off with null data at the origin
        consumedChartData.data.rows.push_back({});
        allocatedChartData.data.rows.push_back({});
        allocationsChartData.data.rows.push_back({});
        // index 0 indicates the total row
        consumedChartData.data.labels[0] = i18n("total");
        allocatedChartData.data.labels[0] = i18n("total");
        allocationsChartData.data.labels[0] = i18n("total");

        buildCharts = true;
        maxConsumedSinceLastTimeStamp = 0;
        vector<ChartMergeData> merged;
        merged.reserve(instructionPointers.size());
        // merge the allocation cost by instruction pointer
        // TODO: aggregate by function instead?
        // TODO: traverse the merged call stack up until the first fork
        for (const auto& alloc : allocations) {
            const auto ip = findTrace(alloc.traceIndex).ipIndex;
            auto it = lower_bound(merged.begin(), merged.end(), ip);
            if (it == merged.end() || it->ip != ip) {
                it = merged.insert(it, {ip, 0, 0, 0});
            }
            it->consumed += alloc.peak; // we want to track the top peaks in the chart
            it->allocated += alloc.allocated;
            it->allocations += alloc.allocations;
        }
        // find the top hot spots for the individual data members and remember their IP and store the label
        auto findTopChartEntries = [&] (quint64 ChartMergeData::* member, ChartDataWithLabels* data) {
            sort(merged.begin(), merged.end(), [=] (const ChartMergeData& left, const ChartMergeData& right) {
                return left.*member > right.*member;
            });
            const size_t MAX_CHART_FUNCTIONS = 20;
            for (size_t i = 0; i < min(size_t(MAX_CHART_FUNCTIONS), merged.size()); ++i) {
                const auto& alloc = merged[i];
                if (!(alloc.*member)) {
                    break;
                }
                const auto ip = alloc.ip;
                data->labelIds[ip] = i + 1;
                const auto function = stringCache.func(findIp(ip));
                data->data.labels[i + 1] = function;
            }
        };
        findTopChartEntries(&ChartMergeData::consumed, &consumedChartData);
        findTopChartEntries(&ChartMergeData::allocated, &allocatedChartData);
        findTopChartEntries(&ChartMergeData::allocations, &allocationsChartData);
    }

    void handleTimeStamp(uint64_t /*oldStamp*/, uint64_t newStamp)
    {
        if (!buildCharts) {
            return;
        }
        maxConsumedSinceLastTimeStamp = max(maxConsumedSinceLastTimeStamp, leaked);
        const uint64_t MAX_CHART_DATAPOINTS = 500; // TODO: make this configurable via the GUI
        const uint64_t diffBetweenTimeStamps = totalTime / MAX_CHART_DATAPOINTS;
        if (newStamp != totalTime && newStamp - lastTimeStamp < diffBetweenTimeStamps) {
            return;
        }
        const auto nowConsumed = maxConsumedSinceLastTimeStamp;
        maxConsumedSinceLastTimeStamp = 0;
        lastTimeStamp = newStamp;

        // create the rows
        auto createRow = [] (uint64_t timeStamp, uint64_t totalCost) {
            ChartRows row;
            row.timeStamp = timeStamp;
            row.cost.insert(0, totalCost);
            return row;
        };
        auto consumed = createRow(newStamp, nowConsumed);
        auto allocated = createRow(newStamp, totalAllocated);
        auto allocs = createRow(newStamp, totalAllocations);

        // if the cost is non-zero and the ip corresponds to a hotspot function selected in the labels,
        // we add the cost to the rows column
        auto addDataToRow = [] (uint64_t cost, IpIndex ip, ChartDataWithLabels* labels, ChartRows* rows) {
            if (!cost) {
                return;
            }
            const auto id = labels->labelIds.value(ip, -1);
            if (id == -1) {
                return;
            }
            rows->cost[id] += cost;
        };
        for (const auto& alloc : allocations) {
            const auto ip = findTrace(alloc.traceIndex).ipIndex;
            addDataToRow(alloc.leaked, ip, &consumedChartData, &consumed);
            addDataToRow(alloc.allocated, ip, &allocatedChartData, &allocated);
            addDataToRow(alloc.allocations, ip, &allocationsChartData, &allocs);
        }
        // add the rows for this time stamp
        consumedChartData.data.rows << consumed;
        allocatedChartData.data.rows << allocated;
        allocationsChartData.data.rows << allocs;
    }

    void handleAllocation()
    {
        maxConsumedSinceLastTimeStamp = max(maxConsumedSinceLastTimeStamp, leaked);
    }

    void handleDebuggee(const char* command)
    {
        debuggee = command;
    }

    string debuggee;

    struct ChartDataWithLabels
    {
        ChartData data;
        QHash<IpIndex, int> labelIds;
    };
    ChartDataWithLabels consumedChartData;
    ChartDataWithLabels allocationsChartData;
    ChartDataWithLabels allocatedChartData;
    uint64_t maxConsumedSinceLastTimeStamp = 0;
    uint64_t lastTimeStamp = 0;

    StringCache stringCache;

    bool buildCharts = false;
};

QString generateSummary(const ParserData& data)
{
    QString ret;
    KFormat format;
    QTextStream stream(&ret);
    const double totalTimeS = 0.001 * data.totalTime;
    stream << "<qt>"
           << i18n("<strong>debuggee</strong>: <code>%1</code>", QString::fromStdString(data.debuggee)) << "<br/>"
           // xgettext:no-c-format
           << i18n("<strong>total runtime</strong>: %1s", totalTimeS) << "<br/>"
           << i18n("<strong>bytes allocated in total</strong> (ignoring deallocations): %1 (%2/s)",
                   format.formatByteSize(data.totalAllocated, 2), format.formatByteSize(data.totalAllocated / totalTimeS)) << "<br/>"
           << i18n("<strong>calls to allocation functions</strong>: %1 (%2/s)",
                   data.totalAllocations, quint64(data.totalAllocations / totalTimeS)) << "<br/>"
           << i18n("<strong>peak heap memory consumption</strong>: %1", format.formatByteSize(data.peak)) << "<br/>"
           << i18n("<strong>total memory leaked</strong>: %1", format.formatByteSize(data.leaked)) << "<br/>";
    stream << "</qt>";
    return ret;
}

void setParents(QVector<RowData>& children, const RowData* parent)
{
    for (auto& row: children) {
        row.parent = parent;
        setParents(row.children, &row);
    }
}

QVector<RowData> mergeAllocations(const ParserData& data)
{
    QVector<RowData> topRows;
    // merge allocations, leave parent pointers invalid (their location may change)
    for (const auto& allocation : data.allocations) {
        auto traceIndex = allocation.traceIndex;
        auto rows = &topRows;
        while (traceIndex) {
            const auto& trace = data.findTrace(traceIndex);
            const auto& ip = data.findIp(trace.ipIndex);
            // TODO: only store the IpIndex and use that
            auto location = data.stringCache.location(ip);
            auto it = lower_bound(rows->begin(), rows->end(), location);
            if (it != rows->end() && it->location == location) {
                it->allocated += allocation.allocated;
                it->allocations += allocation.allocations;
                it->leaked += allocation.leaked;
                it->peak += allocation.peak;
            } else {
                it = rows->insert(it, {allocation.allocations, allocation.peak, allocation.leaked, allocation.allocated,
                                        location, nullptr, {}});
            }
            if (data.isStopIndex(ip.functionIndex)) {
                break;
            }
            traceIndex = trace.parentIndex;
            rows = &it->children;
        }
    }
    // now set the parents, the data is constant from here on
    setParents(topRows, nullptr);
    return topRows;
}

RowData* findByLocation(const RowData& row, QVector<RowData>* data)
{
    for (int i = 0; i < data->size(); ++i) {
        if (data->at(i).location == row.location) {
            return data->data() + i;
        }
    }
    return nullptr;
}

void buildTopDown(const QVector<RowData>& bottomUpData, QVector<RowData>* topDownData)
{
    foreach (const auto& row, bottomUpData) {
        if (row.children.isEmpty()) {
            // leaf node found, bubble up the parent chain to build a top-down tree
            auto node = &row;
            auto stack = topDownData;
            while (node) {
                auto data = findByLocation(*node, stack);
                if (!data) {
                    // create an empty top-down item for this bottom-up node
                    *stack << RowData{0, 0, 0, 0, node->location, nullptr, {}};
                    data = &stack->back();
                }
                // always use the leaf node's cost and propagate that one up the chain
                // otherwise we'd count the cost of some nodes multiple times
                data->allocations += row.allocations;
                data->peak += row.peak;
                data->leaked += row.leaked;
                data->allocated += row.allocated;
                stack = &data->children;
                node = node->parent;
            }
        } else {
            // recurse to find a leaf
            buildTopDown(row.children, topDownData);
        }
    }
}

QVector<RowData> toTopDownData(const QVector<RowData>& bottomUpData)
{
    QVector<RowData> topRows;
    buildTopDown(bottomUpData, &topRows);
    // now set the parents, the data is constant from here on
    setParents(topRows, nullptr);
    return topRows;
}

}

Parser::Parser(QObject* parent)
    : QObject(parent)
{}

Parser::~Parser() = default;

void Parser::parse(const QString& path)
{
    using namespace ThreadWeaver;
    stream() << make_job([=]() {
        const auto stdPath = path.toStdString();
        ParserData data;
        data.read(stdPath);
        data.updateStringCache();

        emit summaryAvailable(generateSummary(data));
        const auto mergedAllocations = mergeAllocations(data);
        emit bottomUpDataAvailable(mergedAllocations);
        // TODO: fork off into two threads here, one for creating top-down + flamegraph
        //       one to do the chart stuff
        const auto topDownData = toTopDownData(mergedAllocations);
        emit topDownDataAvailable(topDownData);
        // TODO: do this on-demand when the flame graph is shown for the first time
        emit flameGraphDataAvailable(FlameGraph::parseData(topDownData));

        data.prepareBuildCharts();
        data.read(stdPath);
        emit consumedChartDataAvailable(data.consumedChartData.data);
        emit allocationsChartDataAvailable(data.allocationsChartData.data);
        emit allocatedChartDataAvailable(data.allocatedChartData.data);

        emit finished();
    });
}
