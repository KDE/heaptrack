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

#ifndef CHARTMODEL_H
#define CHARTMODEL_H

#include <array>

#include <QAbstractTableModel>
#include <QVector>

struct ChartRows
{
    ChartRows()
    {
        cost.fill(0);
    }
    enum
    {
        MAX_NUM_COST = 20
    };
    // time in ms
    qint64 timeStamp = 0;
    std::array<qint64, MAX_NUM_COST> cost;
};
Q_DECLARE_TYPEINFO(ChartRows, Q_MOVABLE_TYPE);

struct ChartData
{
    QVector<ChartRows> rows;
    QHash<int, QString> labels;
};
Q_DECLARE_METATYPE(ChartData)
Q_DECLARE_TYPEINFO(ChartData, Q_MOVABLE_TYPE);

class ChartModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Type
    {
        Consumed,
        Allocations,
        Temporary,
    };
    explicit ChartModel(Type type, QObject* parent = nullptr);
    virtual ~ChartModel();

    Type type() const;

    QVariant headerData(int section, Qt::Orientation orientation = Qt::Horizontal,
                        int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

public slots:
    void resetData(const ChartData& data);
    void clearData();

private:
    ChartData m_data;
    Type m_type;
    // we cache the pens and brushes as constructing them requires allocations
    // otherwise
    QVector<QPen> m_columnDataSetPens;
    QVector<QBrush> m_columnDataSetBrushes;
};

#endif // CHARTMODEL_H
