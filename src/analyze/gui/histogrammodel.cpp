/*
 * Copyright 2015-2017 Milian Wolff <mail@milianw.de>
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

#include "histogrammodel.h"

#include <KChartGlobal>

#include <KFormat>
#include <KLocalizedString>

#include <QBrush>
#include <QColor>
#include <QPen>

#include <limits>

namespace {
QColor colorForColumn(int column, int columnCount)
{
    return QColor::fromHsv((double(column) / columnCount) * 255, 255, 255);
}
}

HistogramModel::HistogramModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    qRegisterMetaType<HistogramData>("HistogramData");
}

HistogramModel::~HistogramModel() = default;

QVariant HistogramModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Vertical && role == Qt::DisplayRole && section >= 0 && section < m_data.size()) {
        return m_data.at(section).sizeLabel;
    }
    return {};
}

QVariant HistogramModel::data(const QModelIndex& index, int role) const
{
    if (!hasIndex(index.row(), index.column(), index.parent())) {
        return {};
    }
    if (role == KChart::DatasetBrushRole) {
        return QVariant::fromValue(QBrush(colorForColumn(index.column(), columnCount())));
    } else if (role == KChart::DatasetPenRole) {
        return QVariant::fromValue(QPen(Qt::black));
    }

    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
        return {};
    }

    const auto& row = m_data.at(index.row());
    const auto& column = row.columns[index.column()];
    if (role == Qt::ToolTipRole) {
        if (index.column() == 0) {
            return i18n("%1 allocations in total", column.allocations);
        }
        return i18n("%1 allocations from %2 in %3 (%4)", column.allocations, column.symbol.symbol, column.symbol.binary,
                    column.symbol.path);
    }
    return column.allocations;
}

int HistogramModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return HistogramRow::NUM_COLUMNS;
}

int HistogramModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_data.size();
}

void HistogramModel::resetData(const HistogramData& data)
{
    beginResetModel();
    m_data = data;
    endResetModel();
}

void HistogramModel::clearData()
{
    beginResetModel();
    m_data = {};
    endResetModel();
}
