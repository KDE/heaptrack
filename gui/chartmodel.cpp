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

#include "chartmodel.h"

#include <KChartGlobal>
#include <KChartLineAttributes>
#include <KLocalizedString>

#include <QPen>
#include <QBrush>
#include <QDebug>

#include "modeltest.h"

namespace {
QColor colorForColumn(int column, int columnCount)
{
    return QColor::fromHsv((1. - double(column + 1) / columnCount) * 255, 255, 255);
}

QVector<ChartRow> columnValue(const ChartRows& data, int column)
{
    if (column == ChartModel::LeakedColumn) {
        return data.leaked;
    } else if (column == ChartModel::AllocationsColumn) {
        return data.allocations;
    } else {
        return data.allocated;
    }
}

ChartRow indexValue(const ChartRows& data, int idx, int column)
{
    const auto& values = columnValue(data, column);
    return idx < values.size() ? values[idx] : ChartRow();
}
}

ChartModel::ChartModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    qRegisterMetaType<ChartData>();
    new ModelTest(this);
}

ChartModel::~ChartModel() = default;

QVariant ChartModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    Q_ASSERT(orientation != Qt::Horizontal || section < columnCount());
    if (orientation == Qt::Horizontal) {
        if (role == KChart::DatasetPenRole) {
            return QVariant::fromValue(QPen(colorForColumn(section, columnCount())));
        } else if (role == KChart::DatasetBrushRole) {
            return QVariant::fromValue(QBrush(colorForColumn(section, columnCount())));
        }
    }
    return {};
}

QVariant ChartModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    Q_ASSERT(index.row() >= 0 && index.row() < rowCount(index.parent()));
    Q_ASSERT(index.column() >= 0 && index.column() < columnCount(index.parent()));
    Q_ASSERT(!index.parent().isValid());

    if ( role == KChart::LineAttributesRole ) {
        KChart::LineAttributes attributes;
        attributes.setDisplayArea(true);
//         if (index == m_selection) {
//             attributes.setTransparency(255);
//         } else {
            attributes.setTransparency(50);
//         }
        return QVariant::fromValue(attributes);
    }

    const auto idx = index.column() / 4;
    const auto column = index.column() % 4;

    if ( role == KChart::DatasetPenRole ) {
        return QVariant::fromValue(QPen(colorForColumn(index.column(), columnCount())));
    } else if ( role == KChart::DatasetBrushRole ) {
        return QVariant::fromValue(QBrush(colorForColumn(index.column(), columnCount())));
    }

    if ( role != Qt::DisplayRole && role != Qt::ToolTipRole ) {
        return {};
    }

    const auto& data = m_data.at(index.row());

    if (column == TimeStampColumn) {
        return data.timeStamp;
    }

    const auto& chartRow = indexValue(data, idx, column);

    if ( role == Qt::ToolTipRole ) {
        // TODO: use correct label for column, format cost and time properly in a human readable way
        return i18n("%1: %2 at %3", chartRow.function, chartRow.cost, data.timeStamp);
    }

    return chartRow.cost;
}

int ChartModel::columnCount(const QModelIndex& /*parent*/) const
{
    return m_data.isEmpty() ? 0 : NUM_COLUMNS * m_data.last().allocated.size();
}

int ChartModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    } else {
        return m_data.size();
    }
}

void ChartModel::resetData(const ChartData& data)
{
    beginResetModel();
    m_data = data;
    endResetModel();
}
