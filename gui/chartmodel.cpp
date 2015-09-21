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
#include <KFormat>

#include <QPen>
#include <QBrush>
#include <QDebug>

#include "modeltest.h"

namespace {
QColor colorForColumn(int column, int columnCount)
{
    return QColor::fromHsv((1. - double(column + 1) / columnCount) * 255, 255, 255);
}

quint64 indexValue(const ChartRows& data, int column, int index)
{
    if (column == ChartModel::LeakedColumn) {
        return data.leaked.value(index);
    } else if (column == ChartModel::AllocationsColumn) {
        return data.allocations.value(index);
    } else {
        return data.allocated.value(index);
    }
}

QString indexLabel(const ChartData& data, int column, int index)
{
    if (column == ChartModel::LeakedColumn) {
        return data.leakedLabels.value(index);
    } else if (column == ChartModel::AllocationsColumn) {
        return data.allocationsLabels.value(index);
    } else {
        return data.allocatedLabels.value(index);
    }
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

        if (role == Qt::DisplayRole || Qt::ToolTipRole) {
            switch (section) {
            case ChartModel::AllocatedColumn:
                return i18n("Memory Allocated");
            case ChartModel::AllocationsColumn:
                return i18n("Memory Allocations");
            case ChartModel::LeakedColumn:
                return i18n("Memory Leaked");
            case ChartModel::TimeStampColumn:
                return i18n("Elapsed Time");
            }
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


    if ( role == KChart::DatasetPenRole ) {
        return QVariant::fromValue(QPen(colorForColumn(index.column(), columnCount())));
    } else if ( role == KChart::DatasetBrushRole ) {
        return QVariant::fromValue(QBrush(colorForColumn(index.column(), columnCount())));
    }

    if ( role != Qt::DisplayRole && role != Qt::ToolTipRole ) {
        return {};
    }

    const auto& data = m_data.rows.at(index.row());
    const auto column = index.column() % NUM_COLUMNS;
    const auto idx = index.column() / NUM_COLUMNS;

    if (column == TimeStampColumn) {
        return data.timeStamp;
    }

    const auto cost = indexValue(data, column, idx);

    if ( role == Qt::ToolTipRole ) {
        const QString time = QString::number(double(data.timeStamp) / 1000, 'g', 3) + QLatin1Char('s');
        if (column == AllocationsColumn) {
            return i18n("%1: %2 at %3", indexLabel(m_data, column, idx), cost, time);
        } else {
            KFormat format;
            return i18n("%1: %2 at %3", indexLabel(m_data, column, idx), format.formatByteSize(cost, 1, KFormat::MetricBinaryDialect), time);
        }
    }

    return cost;
}

int ChartModel::columnCount(const QModelIndex& /*parent*/) const
{
    // the three cost columns + timestamp
    return m_data.allocatedLabels.size() + m_data.allocationsLabels.size() + m_data.leakedLabels.size() + 1;
}

int ChartModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    } else {
        return m_data.rows.size();
    }
}

void ChartModel::resetData(const ChartData& data)
{
    beginResetModel();
    m_data = data;
    endResetModel();
}
