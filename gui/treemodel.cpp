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

#include "treemodel.h"

#include <QDebug>
#include <QTextStream>

#include <KFormat>
#include <KLocalizedString>

#include <cmath>

namespace {

int indexOf(const RowData* row, const TreeData& siblings)
{
    Q_ASSERT(siblings.data() <= row);
    Q_ASSERT(siblings.data() + siblings.size() > row);
    return row - siblings.data();
}

const RowData* rowAt(const TreeData& rows, int row)
{
    Q_ASSERT(rows.size() > row);
    return rows.data() + row;
}

/// @return the parent row containing @p index
const RowData* toParentRow(const QModelIndex& index)
{
    return static_cast<const RowData*>(index.internalPointer());
}

}

TreeModel::TreeModel(QObject* parent)
    : QAbstractItemModel(parent)
{
    qRegisterMetaType<TreeData>();
}

TreeModel::~TreeModel()
{
}

QVariant TreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || section < 0 || section >= NUM_COLUMNS) {
        return {};
    }
    if (role == Qt::InitialSortOrderRole) {
        if (section == AllocatedColumn || section == AllocationsColumn
            || section == PeakColumn || section == LeakedColumn
            || section == TemporaryColumn)
        {
            return Qt::DescendingOrder;
        }
    }
    if (role == Qt::DisplayRole) {
        switch (static_cast<Columns>(section)) {
            case FileColumn:
                return i18n("File");
            case LineColumn:
                return i18n("Line");
            case FunctionColumn:
                return i18n("Function");
            case ModuleColumn:
                return i18n("Module");
            case AllocationsColumn:
                return i18n("Allocations");
            case TemporaryColumn:
                return i18n("Temporary");
            case PeakColumn:
                return i18n("Peak");
            case LeakedColumn:
                return i18n("Leaked");
            case AllocatedColumn:
                return i18n("Allocated");
            case LocationColumn:
                return i18n("Location");
            case NUM_COLUMNS:
                break;
        }
    } else if (role == Qt::ToolTipRole) {
        switch (static_cast<Columns>(section)) {
            case FileColumn:
                return i18n("<qt>The file where the allocation function was called from. "
                            "May be empty when debug information is missing.</qt>");
            case LineColumn:
                return i18n("<qt>The line number where the allocation function was called from. "
                            "May be empty when debug information is missing.</qt>");
            case FunctionColumn:
                return i18n("<qt>The parent function that called an allocation function. "
                            "May be unknown when debug information is missing.</qt>");
            case ModuleColumn:
                return i18n("<qt>The module, i.e. executable or shared library, from which an allocation function was called.</qt>");
            case AllocationsColumn:
                return i18n("<qt>The number of times an allocation function was called from this location.</qt>");
            case TemporaryColumn:
                return i18n("<qt>The number of temporary allocations. These allocations are directly followed by a free without any other allocations in-between.</qt>");
            case PeakColumn:
                return i18n("<qt>The maximum heap memory in bytes consumed from allocations originating at this location. "
                            "This takes deallocations into account.</qt>");
            case LeakedColumn:
                return i18n("<qt>The bytes allocated at this location that have not been deallocated.</qt>");
            case AllocatedColumn:
                return i18n("<qt>The sum of all bytes allocated from this location, ignoring deallocations.</qt>");
            case LocationColumn:
                return i18n("<qt>The location from which an allocation function was called. Function symbol and file information "
                            "may be unknown when debug information was missing when heaptrack was run.</qt>");
            case NUM_COLUMNS:
                break;
        }
    }
    return {};
}

QVariant TreeModel::data(const QModelIndex& index, int role) const
{
    if (index.row() < 0 || index.column() < 0 || index.column() > NUM_COLUMNS) {
        return {};
    }
    const auto row = toRow(index);
    if (role == Qt::DisplayRole || role == SortRole) {
        switch (static_cast<Columns>(index.column())) {
        case AllocatedColumn:
            if (role == SortRole) {
                return row->allocated;
            } else {
                return m_format.formatByteSize(row->allocated);
            }
        case AllocationsColumn:
            return row->allocations;
        case TemporaryColumn:
            return row->temporary;
        case PeakColumn:
            if (role == SortRole) {
                return row->peak;
            } else {
                return m_format.formatByteSize(row->peak);
            }
        case LeakedColumn:
            if (role == SortRole) {
                return row->leaked;
            } else {
                return m_format.formatByteSize(row->leaked);
            }
        case FunctionColumn:
            return row->location->function;
        case ModuleColumn:
            return row->location->module;
        case FileColumn:
            return row->location->file;
        case LineColumn:
            return row->location->line;
        case LocationColumn:
            if (row->location->file.isEmpty()) {
                return i18n("%1 in ?? (%2)", row->location->function,
                            row->location->module);
            } else {
                return i18n("%1 in %2:%3 (%4)", row->location->function,
                            row->location->file, row->location->line,
                            row->location->module);
            }
        case NUM_COLUMNS:
            break;
        }
    } else if (role == Qt::ToolTipRole) {
        QString tooltip;
        QTextStream stream(&tooltip);
        stream << "<qt><pre>";
        stream << i18nc("1: function, 2: file, 3: line, 4: module", "%1\n  at %2:%3\n  in %4",
                        row->location->function, row->location->file, row->location->line, row->location->module);
        stream << '\n';
        KFormat format;
        stream << i18n("allocated %1 over %2 calls (%3 temporary, i.e. %4%), peak at %5, leaked %6",
                       format.formatByteSize(row->allocated), row->allocations, row->temporary,
                       round(float(row->temporary) * 100.f * 100.f / row->allocations) / 100.f,
                       format.formatByteSize(row->peak), format.formatByteSize(row->leaked));
        stream << '\n';
        if (!row->children.isEmpty()) {
            stream << '\n' << i18n("backtrace:") << '\n';
            auto child = row;
            int max = 5;
            while (child->children.count() == 1 && max-- > 0) {
                stream << "\n";
                stream << i18nc("1: function, 2: file, 3: line, 4: module", "%1\n  at %2:%3\n  in %4",
                                child->location->function, child->location->file, child->location->line, child->location->module);
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

QModelIndex TreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (row < 0 || column  < 0 || column >= NUM_COLUMNS || row >= rowCount(parent)) {
        return QModelIndex();
    }
    return createIndex(row, column, const_cast<void*>(reinterpret_cast<const void*>(toRow(parent))));
}

QModelIndex TreeModel::parent(const QModelIndex& child) const
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

int TreeModel::rowCount(const QModelIndex& parent) const
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

int TreeModel::columnCount(const QModelIndex& /*parent*/) const
{
    return NUM_COLUMNS;
}

void TreeModel::resetData(const TreeData& data)
{
    beginResetModel();
    m_data = data;
    endResetModel();
}

const RowData* TreeModel::toRow(const QModelIndex& index) const
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

int TreeModel::rowOf(const RowData* row) const
{
    if (auto parent = row->parent) {
        return indexOf(row, parent->children);
    } else {
        return indexOf(row, m_data);
    }
}
