/*
 * Copyright 2016-2017 Milian Wolff <mail@milianw.de>
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

#include "callercalleemodel.h"

#include <KLocalizedString>

#include <QTextStream>

#include <cmath>

namespace{
/// TODO: share code
QString basename(const QString& path)
{
    int idx = path.lastIndexOf(QLatin1Char('/'));
    return path.mid(idx + 1);
}
}

CallerCalleeModel::CallerCalleeModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    qRegisterMetaType<CallerCalleeRows>();
}

CallerCalleeModel::~CallerCalleeModel() = default;

QVariant CallerCalleeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || section < 0 || section >= NUM_COLUMNS) {
        return {};
    }
    if (role == Qt::InitialSortOrderRole) {
        if (section == SelfAllocatedColumn || section == SelfAllocationsColumn
            || section == SelfPeakColumn || section == SelfLeakedColumn
            || section == SelfTemporaryColumn
            || section == InclusiveAllocatedColumn || section == InclusiveAllocationsColumn
            || section == InclusivePeakColumn || section == InclusiveLeakedColumn
            || section == InclusiveTemporaryColumn)
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
            case SelfAllocationsColumn:
                return i18n("Allocations (Self)");
            case SelfTemporaryColumn:
                return i18n("Temporary (Self)");
            case SelfPeakColumn:
                return i18n("Peak (Self)");
            case SelfLeakedColumn:
                return i18n("Leaked (Self)");
            case SelfAllocatedColumn:
                return i18n("Allocated (Self)");
            case InclusiveAllocationsColumn:
                return i18n("Allocations (Incl.)");
            case InclusiveTemporaryColumn:
                return i18n("Temporary (Incl.)");
            case InclusivePeakColumn:
                return i18n("Peak (Incl.)");
            case InclusiveLeakedColumn:
                return i18n("Leaked (Incl.)");
            case InclusiveAllocatedColumn:
                return i18n("Allocated (Incl.)");
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
            case SelfAllocationsColumn:
                return i18n("<qt>The number of times an allocation function was directly called from this location.</qt>");
            case SelfTemporaryColumn:
                return i18n("<qt>The number of direct temporary allocations. These allocations are directly followed by a free without any other allocations in-between.</qt>");
            case SelfPeakColumn:
                return i18n("<qt>The maximum heap memory in bytes consumed from allocations originating directly at this location. "
                            "This takes deallocations into account.</qt>");
            case SelfLeakedColumn:
                return i18n("<qt>The bytes allocated directly at this location that have not been deallocated.</qt>");
            case SelfAllocatedColumn:
                return i18n("<qt>The sum of all bytes directly allocated from this location, ignoring deallocations.</qt>");
            case InclusiveAllocationsColumn:
                return i18n("<qt>The inclusive number of times an allocation function was called from this location or any functions called from here.</qt>");
            case InclusiveTemporaryColumn:
                return i18n("<qt>The number of inclusive temporary allocations. These allocations are directly followed by a free without any other allocations in-between.</qt>");
            case InclusivePeakColumn:
                return i18n("<qt>The inclusive maximum heap memory in bytes consumed from allocations originating at this location or from functions called from here. "
                            "This takes deallocations into account.</qt>");
            case InclusiveLeakedColumn:
                return i18n("<qt>The bytes allocated at this location that have not been deallocated.</qt>");
            case InclusiveAllocatedColumn:
                return i18n("<qt>The inclusive sum of all bytes allocated from this location or functions called from here, ignoring deallocations.</qt>");
            case LocationColumn:
                return i18n("<qt>The location from which an allocation function was called. Function symbol and file information "
                            "may be unknown when debug information was missing when heaptrack was run.</qt>");
            case NUM_COLUMNS:
                break;
        }
    }
    return {};

}

QVariant CallerCalleeModel::data(const QModelIndex& index, int role) const
{
    if (!hasIndex(index.row(), index.column(), index.parent())) {
        return {};
    }

    const auto& row = (role == MaxCostRole) ? m_maxCost : m_rows.at(index.row());

    if (role == Qt::DisplayRole || role == SortRole || role == MaxCostRole) {
        switch (static_cast<Columns>(index.column())) {
        case SelfAllocatedColumn:
            if (role == SortRole || role == MaxCostRole) {
                return static_cast<qint64>(row.selfCost.allocated);
            } else {
                return m_format.formatByteSize(row.selfCost.allocated);
            }
        case SelfAllocationsColumn:
            return static_cast<qint64>(row.selfCost.allocations);
        case SelfTemporaryColumn:
            return static_cast<qint64>(row.selfCost.temporary);
        case SelfPeakColumn:
            if (role == SortRole || role == MaxCostRole) {
                return static_cast<qint64>(row.selfCost.peak);
            } else {
                return m_format.formatByteSize(row.selfCost.peak);
            }
        case SelfLeakedColumn:
            if (role == SortRole || role == MaxCostRole) {
                return static_cast<qint64>(row.selfCost.leaked);
            } else {
                return m_format.formatByteSize(row.selfCost.leaked);
            }
        case InclusiveAllocatedColumn:
            if (role == SortRole || role == MaxCostRole) {
                return static_cast<qint64>(row.inclusiveCost.allocated);
            } else {
                return m_format.formatByteSize(row.inclusiveCost.allocated);
            }
        case InclusiveAllocationsColumn:
            return static_cast<qint64>(row.inclusiveCost.allocations);
        case InclusiveTemporaryColumn:
            return static_cast<qint64>(row.inclusiveCost.temporary);
        case InclusivePeakColumn:
            if (role == SortRole || role == MaxCostRole) {
                return static_cast<qint64>(row.inclusiveCost.peak);
            } else {
                return m_format.formatByteSize(row.inclusiveCost.peak);
            }
        case InclusiveLeakedColumn:
            if (role == SortRole || role == MaxCostRole) {
                return static_cast<qint64>(row.inclusiveCost.leaked);
            } else {
                return m_format.formatByteSize(row.inclusiveCost.leaked);
            }
        case FunctionColumn:
            return row.location->function;
        case ModuleColumn:
            return row.location->module;
        case FileColumn:
            return row.location->file;
        case LineColumn:
            return row.location->line;
        case LocationColumn:
            if (row.location->file.isEmpty()) {
                return i18n("%1 in ?? (%2)",
                            basename(row.location->function),
                            basename(row.location->module));
            } else {
                return i18n("%1 in %2:%3 (%4)", row.location->function,
                            basename(row.location->file), row.location->line,
                            basename(row.location->module));
            }
        case NUM_COLUMNS:
            break;
        }
    } else if (role == Qt::ToolTipRole) {
        QString tooltip;
        QTextStream stream(&tooltip);
        stream << "<qt><pre style='font-family:monospace;'>";
        if (row.location->line > 0) {
            stream << i18nc("1: function, 2: file, 3: line, 4: module", "%1\n  at %2:%3\n  in %4",
                            row.location->function.toHtmlEscaped(),
                            row.location->file.toHtmlEscaped(), row.location->line,
                            row.location->module.toHtmlEscaped());
        } else {
            stream << i18nc("1: function, 2: module", "%1\n  in %2",
                            row.location->function.toHtmlEscaped(),
                            row.location->module.toHtmlEscaped());
        }
        stream << '\n';
        stream << i18n("inclusive: allocated %1 over %2 calls (%3 temporary, i.e. %4%), peak at %5, leaked %6",
                       m_format.formatByteSize(row.inclusiveCost.allocated), row.inclusiveCost.allocations, row.inclusiveCost.temporary,
                       round(float(row.inclusiveCost.temporary) * 100.f * 100.f / std::max(int64_t(1), row.inclusiveCost.allocations)) / 100.f,
                       m_format.formatByteSize(row.inclusiveCost.peak), m_format.formatByteSize(row.inclusiveCost.leaked));
        stream << '\n';
        stream << i18n("self: allocated %1 over %2 calls (%3 temporary, i.e. %4%), peak at %5, leaked %6",
                       m_format.formatByteSize(row.selfCost.allocated), row.selfCost.allocations, row.selfCost.temporary,
                       round(float(row.selfCost.temporary) * 100.f * 100.f / std::max(int64_t(1), row.selfCost.allocations)) / 100.f,
                       m_format.formatByteSize(row.selfCost.peak), m_format.formatByteSize(row.selfCost.leaked));
        stream << '\n';
        stream << "</pre></qt>";
        return tooltip;
    }
    return {};
}

int CallerCalleeModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : NUM_COLUMNS;
}

int CallerCalleeModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

void CallerCalleeModel::resetData(const QVector<CallerCalleeData>& rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

void CallerCalleeModel::setSummary(const SummaryData& data)
{
    beginResetModel();
    m_maxCost.inclusiveCost = data.cost;
    m_maxCost.selfCost = data.cost;
    endResetModel();
}
