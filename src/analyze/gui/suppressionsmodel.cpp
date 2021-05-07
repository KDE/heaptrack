/*
 * Copyright 2021 Milian Wolff <milian.wolff@kdab.com>
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

#include "suppressionsmodel.h"

#include <analyze/suppressions.h>

#include <summarydata.h>
#include <util.h>

SuppressionsModel::SuppressionsModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

SuppressionsModel::~SuppressionsModel() = default;

void SuppressionsModel::setSuppressions(const SummaryData& summaryData)
{
    beginResetModel();
    m_suppressions = summaryData.suppressions;
    m_totalAllocations = summaryData.cost.allocations;
    m_totalLeaked = summaryData.cost.leaked;
    endResetModel();
}

int SuppressionsModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid() || m_suppressions.empty()) {
        return 0;
    }
    return static_cast<int>(Columns::COLUMN_COUNT);
}

int SuppressionsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_suppressions.size());
}

QVariant SuppressionsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (section < 0 || section >= columnCount() || orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (static_cast<Columns>(section)) {
    case Columns::Matches:
        return tr("Matches");
    case Columns::Leaked:
        return tr("Leaked");
    case Columns::Pattern:
        return tr("Pattern");
    case Columns::COLUMN_COUNT:
        break;
    }
    return {};
}

QVariant SuppressionsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.parent().isValid() || index.column() >= columnCount() || index.row() >= rowCount()) {
        return {};
    }

    const auto& suppression = m_suppressions[index.row()];

    if (role == Qt::ToolTipRole) {
        return tr("<qt>Suppression rule: <code>%1</code><br/>"
                  "Matched Allocations: %2<br/>&nbsp;&nbsp;%3% out of %4 total<br/>"
                  "Suppressed Leaked Memory: %5<br/>&nbsp;&nbsp;%6% out of %7 total</qt>")
            .arg(QString::fromStdString(suppression.pattern), QString::number(suppression.matches),
                 Util::formatCostRelative(suppression.matches, m_totalAllocations), QString::number(m_totalAllocations),
                 Util::formatBytes(suppression.leaked), Util::formatCostRelative(suppression.leaked, m_totalLeaked),
                 Util::formatBytes(m_totalLeaked));
    }

    switch (static_cast<Columns>(index.column())) {
    case Columns::Matches:
        if (role == Qt::DisplayRole || role == SortRole) {
            return static_cast<quint64>(suppression.matches);
        } else if (role == Qt::InitialSortOrderRole) {
            return Qt::DescendingOrder;
        } else if (role == TotalCostRole) {
            return m_totalAllocations;
        }
        break;
    case Columns::Leaked:
        if (role == Qt::DisplayRole) {
            return Util::formatBytes(suppression.leaked);
        } else if (role == SortRole) {
            return static_cast<qint64>(suppression.leaked);
        } else if (role == Qt::InitialSortOrderRole) {
            return Qt::DescendingOrder;
        } else if (role == TotalCostRole) {
            return m_totalLeaked;
        }
        break;
    case Columns::Pattern:
        if (role == Qt::DisplayRole || role == SortRole) {
            return QString::fromStdString(suppression.pattern);
        } else if (role == Qt::InitialSortOrderRole) {
            return Qt::AscendingOrder;
        }
        break;
    case Columns::COLUMN_COUNT:
        break;
    }

    return {};
}
