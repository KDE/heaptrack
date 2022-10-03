/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef CHARTMODEL_H
#define CHARTMODEL_H

#include <array>

#include <QAbstractTableModel>
#include <QVector>

#include "resultdata.h"

#include <memory>

struct ChartRows
{
    ChartRows()
    {
        cost.fill(0);
    }
    enum
    {
        MAX_NUM_COST = 21
    };
    // time in ms
    qint64 timeStamp = 0;
    std::array<qint64, MAX_NUM_COST> cost;
};
Q_DECLARE_TYPEINFO(ChartRows, Q_MOVABLE_TYPE);

struct ChartData
{
    QVector<ChartRows> rows;
    QHash<int, Symbol> labels;
    std::shared_ptr<const ResultData> resultData;
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

    int maximumDatasetCount() const
    {
        return m_maxDatasetCount;
    }
    void setMaximumDatasetCount(int count);

    qint64 totalCostAt(qint64 timeStamp) const;

public slots:
    void resetData(const ChartData& data);
    void clearData();

private:
    void resetColors();

    ChartData m_data;
    Type m_type;
    // we cache the pens and brushes as constructing them requires allocations
    // otherwise
    QVector<QPen> m_columnDataSetPens;
    QVector<QBrush> m_columnDataSetBrushes;
    int m_maxDatasetCount;
};

#endif // CHARTMODEL_H
