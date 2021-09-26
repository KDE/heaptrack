/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "treemodel.h"

#include <QDebug>
#include <QTextStream>

#include <KLocalizedString>

#include <cmath>

#include "resultdata.h"
#include "util.h"

namespace {

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
        if (section == AllocationsColumn || section == PeakColumn || section == LeakedColumn
            || section == TemporaryColumn) {
            return Qt::DescendingOrder;
        }
    }
    if (role == Qt::DisplayRole) {
        switch (static_cast<Columns>(section)) {
        case AllocationsColumn:
            return i18n("Allocations");
        case TemporaryColumn:
            return i18n("Temporary");
        case PeakColumn:
            return i18n("Peak");
        case LeakedColumn:
            return i18n("Leaked");
        case LocationColumn:
            return i18n("Location");
        case NUM_COLUMNS:
            break;
        }
    } else if (role == Qt::ToolTipRole) {
        switch (static_cast<Columns>(section)) {
        case AllocationsColumn:
            return i18n("<qt>The number of times an allocation function was called "
                        "from this location.</qt>");
        case TemporaryColumn:
            return i18n("<qt>The number of temporary allocations. These allocations "
                        "are directly followed by a free "
                        "without any other allocations in-between.</qt>");
        case PeakColumn:
            return i18n("<qt>The contributions from a given location to the maximum heap "
                        "memory consumption in bytes. This takes deallocations "
                        "into account.</qt>");
        case LeakedColumn:
            return i18n("<qt>The bytes allocated at this location that have not been "
                        "deallocated.</qt>");
        case LocationColumn:
            return i18n("<qt>The location from which an allocation function was "
                        "called. Function symbol and file "
                        "information "
                        "may be unknown when debug information was missing when "
                        "heaptrack was run.</qt>");
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

    const auto row = (role == MaxCostRole) ? &m_maxCost : toRow(index);

    if (role == Qt::DisplayRole || role == SortRole || role == MaxCostRole) {
        switch (static_cast<Columns>(index.column())) {
        case AllocationsColumn:
            if (role == SortRole || role == MaxCostRole) {
                return static_cast<qint64>(abs(row->cost.allocations));
            }
            return static_cast<qint64>(row->cost.allocations);
        case TemporaryColumn:
            if (role == SortRole || role == MaxCostRole) {
                return static_cast<qint64>(abs(row->cost.temporary));
            }
            return static_cast<qint64>(row->cost.temporary);
        case PeakColumn:
            if (role == SortRole || role == MaxCostRole) {
                return static_cast<qint64>(abs(row->cost.peak));
            } else {
                return Util::formatBytes(row->cost.peak);
            }
        case LeakedColumn:
            if (role == SortRole || role == MaxCostRole) {
                return static_cast<qint64>(abs(row->cost.leaked));
            } else {
                return Util::formatBytes(row->cost.leaked);
            }
        case LocationColumn:
            return Util::toString(row->symbol, *m_data.resultData, Util::Short);
        case NUM_COLUMNS:
            break;
        }
    } else if (role == Qt::ToolTipRole) {
        auto toStr = [this](StringIndex stringId) { return m_data.resultData->string(stringId); };
        QString tooltip;
        QTextStream stream(&tooltip);
        stream << "<qt><pre style='font-family:monospace;'>";
        const auto module = toStr(row->symbol.moduleId);
        stream << i18nc("1: function, 2: module, 3: module path", "%1\n  in %2 (%3)",
                        toStr(row->symbol.functionId).toHtmlEscaped(), Util::basename(module).toHtmlEscaped(),
                        module.toHtmlEscaped());
        stream << '\n';
        stream << '\n';
        const auto peakFraction = Util::formatCostRelative(row->cost.peak, m_maxCost.cost.peak);
        const auto leakedFraction = Util::formatCostRelative(row->cost.leaked, m_maxCost.cost.leaked);
        const auto allocationsFraction = Util::formatCostRelative(row->cost.allocations, m_maxCost.cost.allocations);
        const auto temporaryFraction = Util::formatCostRelative(row->cost.temporary, row->cost.allocations);
        const auto temporaryFractionTotal = Util::formatCostRelative(row->cost.temporary, m_maxCost.cost.temporary);
        stream << i18n("peak contribution: %1 (%2% of total)\n", Util::formatBytes(row->cost.peak), peakFraction);
        stream << i18n("leaked: %1 (%2% of total)\n", Util::formatBytes(row->cost.leaked), leakedFraction);
        stream << i18n("allocations: %1 (%2% of total)\n", row->cost.allocations, allocationsFraction);
        stream << i18n("temporary: %1 (%2% of allocations, %3% of total)\n", row->cost.temporary, temporaryFraction,
                       temporaryFractionTotal);
        if (!row->children.isEmpty()) {
            auto child = row;
            int max = 5;
            if (child->children.count() == 1) {
                stream << '\n' << i18n("backtrace:") << '\n';
            }
            while (child->children.count() == 1 && max-- > 0) {
                stream << "\n";
                const auto module = toStr(child->symbol.moduleId);
                stream << i18nc("1: function, 2: module, 3: module path", "%1\n  in %2 (%3)",
                                toStr(child->symbol.functionId).toHtmlEscaped(), Util::basename(module).toHtmlEscaped(),
                                module.toHtmlEscaped());
                child = child->children.data();
            }
            if (child->children.count() > 1) {
                stream << "\n";
                stream << i18np("called from one location", "called from %1 locations", child->children.count());
            }
        }
        stream << "</pre></qt>";
        return tooltip;
    } else if (role == SymbolRole) {
        return QVariant::fromValue(row->symbol);
    } else if (role == ResultDataRole) {
        return QVariant::fromValue(m_data.resultData.get());
    }
    return {};
}

QModelIndex TreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (row < 0 || column < 0 || column >= NUM_COLUMNS || row >= rowCount(parent)) {
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
        return m_data.rows.size();
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
    Q_ASSERT(data.resultData);
    beginResetModel();
    m_data = data;
    endResetModel();
}

void TreeModel::setSummary(const SummaryData& data)
{
    beginResetModel();
    m_maxCost.cost = data.cost;
    endResetModel();
}

void TreeModel::clearData()
{
    beginResetModel();
    m_data = {};
    m_maxCost = {};
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
        return rowAt(m_data.rows, index.row());
    }
}

int TreeModel::rowOf(const RowData* row) const
{
    if (auto parent = row->parent) {
        return indexOf(row, parent->children);
    } else {
        return indexOf(row, m_data.rows);
    }
}
