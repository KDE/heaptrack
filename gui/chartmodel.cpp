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

#include <QPen>
#include <QBrush>

#include "modeltest.h"

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
    if (section == 0 && orientation == Qt::Horizontal) {
        if ( role == KChart::DatasetPenRole ) {
            return QPen(Qt::red);
        } else if ( role == KChart::DatasetBrushRole ) {
            return QBrush(Qt::red);
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
    Q_ASSERT(m_data);
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
    if ( role == KChart::DatasetPenRole ) {
        static const auto pen = QVariant::fromValue(QPen(Qt::red));
        return pen;
    } else if ( role == KChart::DatasetBrushRole ) {
        static const auto brush = QVariant::fromValue(QBrush(Qt::red));
        return brush;
    }

    if ( role != Qt::DisplayRole && role != Qt::ToolTipRole ) {
        return {};
    }

    if ( role == Qt::ToolTipRole ) {
        return {};
    }

    const auto& data= m_data.at(index.row());
    if (index.column() == 0) {
        return data.timeStamp;
    } else if (index.column() == 1) {
        return data.leaked;
    } else if (index.column() == 2) {
        return data.allocations;
    } else {
        return data.allocated;
    }
}

int ChartModel::columnCount(const QModelIndex& /*parent*/) const
{
    return 4;
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
