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

#ifndef HISTOGRAMMODEL_H
#define HISTOGRAMMODEL_H

#include <QAbstractTableModel>

#include "treemodel.h"

struct HistogramColumn
{
    qint64 allocations;
    Symbol symbol;
};
Q_DECLARE_TYPEINFO(HistogramColumn, Q_MOVABLE_TYPE);

struct HistogramRow
{
    HistogramRow()
    {
        columns.fill({0, {}});
    }
    enum
    {
        NUM_COLUMNS = 10 + 1
    };
    QString sizeLabel;
    quint64 size = 0;
    std::array<HistogramColumn, NUM_COLUMNS> columns;
};
Q_DECLARE_TYPEINFO(HistogramRow, Q_MOVABLE_TYPE);
Q_DECLARE_METATYPE(HistogramRow)

using HistogramData = QVector<HistogramRow>;

class HistogramModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit HistogramModel(QObject* parent = nullptr);
    ~HistogramModel() override;

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;

    void resetData(const HistogramData& data);
    void clearData();

private:
    HistogramData m_data;
};

#endif // HISTOGRAMMODEL_H
