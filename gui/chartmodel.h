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

#ifndef CHARTMODEL_H
#define CHARTMODEL_H

#include <QAbstractTableModel>
#include <QVector>

struct ChartRow
{
    QString function;
    quint64 cost;
};
Q_DECLARE_TYPEINFO(ChartRow, Q_MOVABLE_TYPE);

struct ChartRows
{
    quint64 timeStamp;
    QVector<ChartRow> leaked;
    QVector<ChartRow> allocations;
    QVector<ChartRow> allocated;
};
Q_DECLARE_TYPEINFO(ChartRows, Q_MOVABLE_TYPE);

using ChartData = QVector<ChartRows>;
Q_DECLARE_METATYPE(ChartData)

class ChartModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit ChartModel(QObject* parent = nullptr);
    virtual ~ChartModel();

    enum Columns {
        TimeStampColumn,
        LeakedColumn,
        AllocationsColumn,
        AllocatedColumn,
        NUM_COLUMNS
    };

    QVariant headerData(int section, Qt::Orientation orientation = Qt::Horizontal, int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

public slots:
    void resetData(const ChartData& data);

private:
    ChartData m_data;
};

#endif // CHARTMODEL_H
