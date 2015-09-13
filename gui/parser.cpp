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
    }

    QString func(const InstructionPointer& ip) const
    {
        if (ip.functionIndex) {
            // TODO: support removal of template arguments
            return stringify(ip.functionIndex);
        } else {
            return static_cast<QString>(QLatin1String("0x") + QString::number(ip.instructionPointer, 16));
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
};

struct ChartMergeData
{
    QString function;
    quint64 leaked;
    quint64 allocations;
    quint64 allocated;
    bool operator<(const QString& rhs) const
    {
        return function < rhs;
    }
};

struct ParserData final : public AccumulatedTraceData
{
    ParserData()
    {
        chartData.push_back({0, {{i18n("total"), 0}}, {{i18n("total"), 0}}, {{i18n("total"), 0}}});
    }

    void handleTimeStamp(uint64_t /*newStamp*/, uint64_t oldStamp)
    {
        stringCache.update(strings);
        maxLeakedSinceLastTimeStamp = max(maxLeakedSinceLastTimeStamp, leaked);
        ChartRows data;
        data.timeStamp = oldStamp;
        data.leaked.push_back({i18n("total"), maxLeakedSinceLastTimeStamp});
        data.allocations.push_back({i18n("total"), totalAllocations});
        data.allocated.push_back({i18n("total"), totalAllocated});

        vector<ChartMergeData> mergedData;
        for (const auto& allocation : allocations) {
            const auto function = stringCache.func(findIp(findTrace(allocation.traceIndex).ipIndex));
            auto it = lower_bound(mergedData.begin(), mergedData.end(), function);
            if (it != mergedData.end() && it->function == function) {
                it->allocated += allocation.allocated;
                it->allocations += allocation.allocations;
                it->leaked += allocation.leaked;
            } else {
                it = mergedData.insert(it, {function, allocation.leaked, allocation.allocations, allocation.allocated});
            }
        }

        // TODO: deduplicate code
        sort(mergedData.begin(), mergedData.end(), [] (const ChartMergeData& left, const ChartMergeData& right) {
            return left.leaked > right.leaked;
        });
        for (size_t i = 0; i < min(size_t(10), mergedData.size()); ++i) {
            const auto& alloc = mergedData[i];
            if (!alloc.leaked)
                break;
            data.leaked.push_back({alloc.function, alloc.leaked});
        }
        sort(mergedData.begin(), mergedData.end(), [] (const ChartMergeData& left, const ChartMergeData& right) {
            return left.allocations > right.allocations;
        });
        for (size_t i = 0; i < min(size_t(10), mergedData.size()); ++i) {
            const auto& alloc = mergedData[i];
            if (!alloc.allocations)
                break;
            data.allocations.push_back({alloc.function, alloc.allocations});
        }
        sort(mergedData.begin(), mergedData.end(), [] (const ChartMergeData& left, const ChartMergeData& right) {
            return left.allocated > right.allocated;
        });
        for (size_t i = 0; i < min(size_t(10), mergedData.size()); ++i) {
            const auto& alloc = mergedData[i];
            if (!alloc.allocated)
                break;
            data.allocated.push_back({alloc.function, alloc.allocated});
        }
        chartData.push_back(data);
        maxLeakedSinceLastTimeStamp = 0;
    }

    void handleAllocation()
    {
        maxLeakedSinceLastTimeStamp = max(maxLeakedSinceLastTimeStamp, leaked);
    }

    void handleDebuggee(const char* command)
    {
        debuggee = command;
    }

    string debuggee;

    // FIXME: keep order of entries constant, add zero elements where required
    ChartData chartData;
    uint64_t maxLeakedSinceLastTimeStamp = 0;

    StringCache stringCache;
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
        ParserData data;
        data.read(path.toStdString());
        emit summaryAvailable(generateSummary(data));
        const auto mergedAllocations = mergeAllocations(data);
        emit bottomUpDataAvailable(mergedAllocations);
        emit chartDataAvailable(data.chartData);
        const auto topDownData = toTopDownData(mergedAllocations);
        emit topDownDataAvailable(topDownData);
        emit flameGraphDataAvailable(FlameGraph::parseData(topDownData));
        emit finished();
    });
}
