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

#include "model.h"

#include <QDebug>
#include <QTextStream>

#include <KFormat>
#include <KLocalizedString>
#include <ThreadWeaver/ThreadWeaver>

#include <sstream>
#include <cmath>

#include "../accumulatedtracedata.h"

using namespace std;

namespace {
QString generateSummary(const AccumulatedTraceData& data)
{
    QString ret;
    KFormat format;
    QTextStream stream(&ret);
    const double totalTimeS = 0.001 * data.totalTime;
    /// TODO: translate
    stream << "<qt>"
           << "<strong>total runtime</strong>: " << totalTimeS << "s.<br/>"
           << "<strong>bytes allocated in total</strong> (ignoring deallocations): " << format.formatByteSize(data.totalAllocated, 2)
             << " (" << format.formatByteSize(data.totalAllocated / totalTimeS) << "/s)<br/>"
           << "<strong>calls to allocation functions</strong>: " << data.totalAllocations
             << " (" << size_t(data.totalAllocations / totalTimeS) << "/s)<br/>"
           << "<strong>peak heap memory consumption</strong>: " << format.formatByteSize(data.peak) << "<br/>"
           << "<strong>total memory leaked</strong>: " << format.formatByteSize(data.leaked) << "<br/>";
    stream << "</qt>";
    return ret;
}

int indexOf(const RowData* row, const QVector<RowData>& siblings)
{
    Q_ASSERT(siblings.data() <= row);
    Q_ASSERT(siblings.data() + siblings.size() > row);
    return row - siblings.data();
}

const RowData* rowAt(const QVector<RowData>& rows, int row)
{
    Q_ASSERT(rows.size() > row);
    return rows.data() + row;
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

Model::Model(QObject* parent)
    : QAbstractItemModel(parent)
{
    qRegisterMetaType<QVector<RowData>>();
    connect(this, &Model::dataReadyBackground,
            this, &Model::dataReadyForeground);
}

Model::~Model()
{
}

QVariant Model::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 || section >= NUM_COLUMNS) {
        return {};
    }
    switch (static_cast<Columns>(section)) {
        case FileColumn:
            return tr("File");
        case FunctionColumn:
            return tr("Function");
        case ModuleColumn:
            return tr("Module");
        case AllocationsColumn:
            return tr("Allocations");
        case PeakColumn:
            return tr("Peak");
        case LeakedColumn:
            return tr("Leaked");
        case AllocatedColumn:
            return tr("Allocated");
        case NUM_COLUMNS:
            break;
    }
    return {};
}

QVariant Model::data(const QModelIndex& index, int role) const
{
    if (index.row() < 0 || index.column() < 0 || index.column() > NUM_COLUMNS) {
        return {};
    }
    const auto row = toRow(index);
    if (role == Qt::DisplayRole) {
        switch (static_cast<Columns>(index.column())) {
        case AllocatedColumn:
            return row->allocated;
        case AllocationsColumn:
            return row->allocations;
        case PeakColumn:
            return row->peak;
        case LeakedColumn:
            return row->leaked;
        case FunctionColumn:
            return row->location.function;
        case ModuleColumn:
            return row->location.module;
        case FileColumn:
            return row->location.file;
        case NUM_COLUMNS:
            break;
        }
    } else if (role == Qt::ToolTipRole) {
        QString tooltip;
        QTextStream stream(&tooltip);
        stream << "<qt><pre>";
        stream << i18nc("1: function, 2: file, 3: module", "%1\n  at %2\n  in %3",
                        row->location.function, row->location.file, row->location.module);
        stream << '\n';
        KFormat format;
        stream << i18n("allocated %1 over %2 calls, peak at %3, leaked %4",
                       format.formatByteSize(row->allocated), row->allocations,
                       format.formatByteSize(row->peak), format.formatByteSize(row->leaked));
        stream << '\n';
        if (!row->children.isEmpty()) {
            stream << '\n' << i18n("backtrace:") << '\n';
            auto child = row;
            int max = 5;
            while (child->children.count() == 1 && max-- > 0) {
                stream << "\n";
                stream << i18nc("1: function, 2: file, 3: module", "%1\n  at %2\n  in %3",
                                child->location.function, child->location.file, child->location.module);
                child = child->children.data();
            }
            if (child->children.count() > 1) {
                stream << "\n";
                stream << i18np("called from one location", "called from %1 locations", child->children.count());
            }
        }
        stream << "</pre></qt>";
        return tooltip;
    }
    return {};
}

QModelIndex Model::index(int row, int column, const QModelIndex& parent) const
{
    if (row < 0 || column  < 0 || column >= NUM_COLUMNS || row >= rowCount(parent)) {
        return QModelIndex();
    }
    return createIndex(row, column, const_cast<void*>(reinterpret_cast<const void*>(toRow(parent))));
}

QModelIndex Model::parent(const QModelIndex& child) const
{
    if (!child.isValid()) {
        return {};
    }
    const auto parent = toParentRow(child);
    if (!parent) {
        return {};
    }
    return createIndex(rowOf(parent), 0, const_cast<void*>(reinterpret_cast<const void*>(parent->parent)));
}

int Model::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid()) {
        return m_data.size();
    } else if (parent.column() != 0) {
        return 0;
    }
    auto row = toRow(parent);
    Q_ASSERT(row);
    return row->children.size();
}

int Model::columnCount(const QModelIndex& /*parent*/) const
{
    return NUM_COLUMNS;
}

void Model::loadFile(const QString& path)
{
    qDebug() << "load file" << path;
    using namespace ThreadWeaver;
    stream() << make_job([=]() {
        AccumulatedTraceData data;
        data.read(path.toStdString());
        emit dataReadyBackground(mergeAllocations(data), generateSummary(data));
    });
}

void Model::dataReadyForeground(const QVector<RowData>& data, const QString& summary)
{
    beginResetModel();
    m_data = data;
    endResetModel();
    emit dataReady(summary);
}

const RowData* Model::toRow(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return nullptr;
    }
    if (const auto parent = toParentRow(index)) {
        return rowAt(parent->children, index.row());
    } else {
        return rowAt(m_data, index.row());
    }
}

const RowData* Model::toParentRow(const QModelIndex& index) const
{
    Q_ASSERT(index.isValid());
    return static_cast<const RowData*>(index.internalPointer());
}

int Model::rowOf(const RowData* row) const
{
    if (auto parent = row->parent) {
        return indexOf(row, parent->children);
    } else {
        return indexOf(row, m_data);
    }
}
