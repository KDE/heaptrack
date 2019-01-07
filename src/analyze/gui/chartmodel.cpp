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

#include "chartmodel.h"

#include <KChartGlobal>
#include <KChartLineAttributes>
#include <KFormat>
#include <KLocalizedString>

#include <QBrush>
#include <QDebug>
#include <QPen>

#include "util.h"

namespace {
QColor colorForColumn(int column, int columnCount)
{
    return QColor::fromHsv((double(column + 1) / columnCount) * 255, 255, 255);
}
}

ChartModel::ChartModel(Type type, QObject* parent)
    : QAbstractTableModel(parent)
    , m_type(type)
{
    qRegisterMetaType<ChartData>();
}

ChartModel::~ChartModel() = default;

ChartModel::Type ChartModel::type() const
{
    return m_type;
}

QVariant ChartModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    Q_ASSERT(orientation == Qt::Horizontal || section < columnCount());
    if (orientation == Qt::Horizontal) {
        if (role == KChart::DatasetPenRole) {
            return QVariant::fromValue(m_columnDataSetPens.at(section));
        } else if (role == KChart::DatasetBrushRole) {
            return QVariant::fromValue(m_columnDataSetBrushes.at(section));
        }

        if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
            if (section == 0) {
                return i18n("Elapsed Time");
            }
            switch (m_type) {
            case Allocations:
                return i18n("Memory Allocations");
            case Consumed:
                return i18n("Memory Consumed");
            case Temporary:
                return i18n("Temporary Allocations");
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

    if (role == KChart::LineAttributesRole) {
        KChart::LineAttributes attributes;
        attributes.setDisplayArea(true);
        if (index.column() > 1) {
            attributes.setTransparency(127);
        } else {
            attributes.setTransparency(50);
        }
        return QVariant::fromValue(attributes);
    }

    if (role == KChart::DatasetPenRole) {
        return QVariant::fromValue(m_columnDataSetPens.at(index.column()));
    } else if (role == KChart::DatasetBrushRole) {
        return QVariant::fromValue(m_columnDataSetBrushes.at(index.column()));
    }

    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
        return {};
    }

    const auto& data = m_data.rows.at(index.row());

    int column = index.column();
    if (role != Qt::ToolTipRole && column % 2 == 0) {
        return data.timeStamp;
    }
    column = column / 2;
    Q_ASSERT(column < ChartRows::MAX_NUM_COST);

    const auto cost = data.cost[column];
    if (role == Qt::ToolTipRole) {
        const QString time = Util::formatTime(data.timeStamp);
        auto byteCost = [cost]() -> QString {
            KFormat format;
            const auto formatted = format.formatByteSize(cost, 1, KFormat::MetricBinaryDialect);
            if (cost > 1024) {
                return i18nc("%1: the formatted byte size, e.g. \"1.2KB\", %2: the raw byte size, e.g. \"1300\"",
                             "%1 (%2 bytes)", formatted, cost);
            } else {
                return formatted;
            }
        };
        if (column == 0) {
            switch (m_type) {
            case Allocations:
                return i18n("<qt>%1 allocations in total after %2</qt>", cost, time);
            case Temporary:
                return i18n("<qt>%1 temporary allocations in total after %2</qt>", cost, time);
            case Consumed:
                return i18n("<qt>%1 consumed in total after %2</qt>", byteCost(), time);
            }
        } else {
            const auto label = m_data.labels.value(column).toHtmlEscaped();
            switch (m_type) {
            case Allocations:
                return i18n("<qt>%2 allocations after %3 from:<p "
                            "style='margin-left:10px;'>%1</p></qt>",
                            label, cost, time);
            case Temporary:
                return i18n("<qt>%2 temporary allocations after %3 from:<p "
                            "style='margin-left:10px'>%1</p></qt>",
                            label, cost, time);
            case Consumed:
                return i18n("<qt>%2 consumed after %3 from:<p "
                            "style='margin-left:10px'>%1</p></qt>",
                            label, byteCost(), time);
            }
        }
        return {};
    }

    return cost;
}

int ChartModel::columnCount(const QModelIndex& /*parent*/) const
{
    return m_data.labels.size() * 2;
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
    Q_ASSERT(m_data.labels.size() < ChartRows::MAX_NUM_COST);
    beginResetModel();
    m_data = data;
    m_columnDataSetBrushes.clear();
    m_columnDataSetPens.clear();
    const auto columns = columnCount();
    for (int i = 0; i < columns; ++i) {
        auto color = colorForColumn(i, columns);
        m_columnDataSetBrushes << QBrush(color);
        m_columnDataSetPens << QPen(color);
    }
    endResetModel();
}

void ChartModel::clearData()
{
    beginResetModel();
    m_data = {};
    m_columnDataSetBrushes = {};
    m_columnDataSetPens = {};
    endResetModel();
}
