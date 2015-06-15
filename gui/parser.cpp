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

#include "../accumulatedtracedata.h"

#include <vector>

using namespace std;

namespace {
struct ParserData final : public AccumulatedTraceData
{
    ParserData()
    {
    }

    void handleTimeStamp(size_t /*newStamp*/, size_t oldStamp)
    {
        m_leaked.push_back({static_cast<quint64>(oldStamp), static_cast<quint64>(timestampData.leaked)});
        timestampData = {};
    }

    void handleAllocation()
    {
        timestampData.leaked = max(timestampData.leaked, leaked);
    }

    void handleDebuggee(const char* command)
    {
        debuggee = command;
    }

    string debuggee;

    ChartData m_leaked;
    struct TimestampData {
        size_t leaked = 0;
    };
    TimestampData timestampData;
};

QString generateSummary(const ParserData& data)
{
    QString ret;
    KFormat format;
    QTextStream stream(&ret);
    const double totalTimeS = 0.001 * data.totalTime;
    stream << "<qt>"
           << i18n("<strong>debuggee</strong>: <code>%1</code>", QString::fromStdString(data.debuggee)) << "<br/>"
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

struct StringCache
{
    StringCache(const AccumulatedTraceData& data)
    {
        m_strings.resize(data.strings.size());
        transform(data.strings.begin(), data.strings.end(),
                  m_strings.begin(), [] (const string& str) { return QString::fromStdString(str); });
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
            auto file = stringify(ip.fileIndex);
            return file + QLatin1Char(':') + QString::number(ip.line);
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
        return {func(ip), file(ip), module(ip)};
    }

    vector<QString> m_strings;
};

void setParents(QVector<RowData>& children, const RowData* parent)
{
    for (auto& row: children) {
        row.parent = parent;
        setParents(row.children, &row);
    }
}

QVector<RowData> mergeAllocations(const AccumulatedTraceData& data)
{
    QVector<RowData> topRows;
    StringCache strings(data);
    // merge allocations, leave parent pointers invalid (their location may change)
    for (const auto& allocation : data.allocations) {
        auto traceIndex = allocation.traceIndex;
        auto rows = &topRows;
        while (traceIndex) {
            const auto& trace = data.findTrace(traceIndex);
            const auto& ip = data.findIp(trace.ipIndex);
            // TODO: only store the IpIndex and use that
            auto location = strings.location(ip);
            auto it = lower_bound(rows->begin(), rows->end(), location);
            if (it != rows->end() && it->location == location) {
                it->allocated += allocation.allocated;
                it->allocations += allocation.allocations;
                it->leaked += allocation.leaked;
                it->peak = max(it->peak, static_cast<quint64>(allocation.peak));
            } else {
                it = rows->insert(it, {allocation.allocations, allocation.allocated, allocation.leaked, allocation.peak,
                                        location, nullptr, {}});
            }
            traceIndex = trace.parentIndex;
            rows = &it->children;
        }
    }
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
        emit bottomUpDataAvailable(mergeAllocations(data));
        emit leakedDataAvailable(data.m_leaked);
        emit finished();
    });
}
