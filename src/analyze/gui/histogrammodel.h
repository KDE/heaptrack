/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
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

struct HistogramData
{
    QVector<HistogramRow> rows;
    std::shared_ptr<const ResultData> resultData;
};
Q_DECLARE_METATYPE(HistogramData)

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
