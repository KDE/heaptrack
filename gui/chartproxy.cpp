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

#include "chartproxy.h"
#include "chartmodel.h"

#include <QDebug>

ChartProxy::ChartProxy(const QString& label, int column, QObject* parent)
    : QSortFilterProxyModel(parent)
    , m_label(label)
    , m_column(column)
{}

ChartProxy::~ChartProxy() = default;

QVariant ChartProxy::headerData(int section, Qt::Orientation orientation, int role) const
{
    Q_ASSERT(orientation != Qt::Horizontal || section < columnCount());
    if (section == 0 && orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        return m_label;
    }
    return QSortFilterProxyModel::headerData(section, orientation, role);
}

QVariant ChartProxy::data(const QModelIndex& proxyIndex, int role) const
{
    static_assert(ChartModel::TimeStampColumn == 0, "The code below assumes the time stamp column comes with value 0.");
    if (role == Qt::ToolTipRole && proxyIndex.column() == 0) {
        // KChart queries the tooltip for the timestamp column, which is not useful for us
        // instead, we want to use the m_column, or in proxy column value that is 1
        return QSortFilterProxyModel::data(index(proxyIndex.row(), 1, proxyIndex.parent()), role);
    } else {
        return QSortFilterProxyModel::data(proxyIndex, role);
    }
}

bool ChartProxy::filterAcceptsColumn(int sourceColumn, const QModelIndex& /*sourceParent*/) const
{
    const auto column = sourceColumn % 4;
    return column == ChartModel::TimeStampColumn || column == m_column;
}
