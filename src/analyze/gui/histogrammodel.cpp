/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "histogrammodel.h"

#include <KChartGlobal>

#include <KLocalizedString>

#include <QBrush>
#include <QColor>
#include <QPen>

#include <limits>

#include <util.h>

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
    if (orientation == Qt::Vertical && role == Qt::DisplayRole && section >= 0 && section < m_data.rows.size()) {
        return m_data.rows.at(section).sizeLabel;
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

    const auto& row = m_data.rows.at(index.row());
    const auto& column = row.columns[index.column()];
    if (role == Qt::ToolTipRole) {
        if (index.column() == 0) {
            return i18n("%1 allocations in total", column.allocations);
        }
        return i18n("%1 allocations from %2, totalling %3 allocated with an average of %4 per allocation",
                    column.allocations, Util::toString(column.symbol, *m_data.resultData, Util::Long),
                    Util::formatBytes(column.totalAllocated),
                    Util::formatBytes(column.totalAllocated / column.allocations));
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
    return m_data.rows.size();
}

void HistogramModel::resetData(const HistogramData& data)
{
    Q_ASSERT(data.resultData);
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

#include "moc_histogrammodel.cpp"
