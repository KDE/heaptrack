/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef TREEMODEL_H
#define TREEMODEL_H

#include <QAbstractItemModel>
#include <QVector>

#include "../allocationdata.h"
#include "locationdata.h"
#include "summarydata.h"

#include <memory>

class ResultData;

struct RowData
{
    AllocationData cost;
    Symbol symbol;
    const RowData* parent;
    QVector<RowData> children;
    bool operator<(const Symbol& rhs) const
    {
        return symbol < rhs;
    }
};
Q_DECLARE_TYPEINFO(RowData, Q_MOVABLE_TYPE);

struct TreeData
{
    QVector<RowData> rows;
    std::shared_ptr<const ResultData> resultData;
};
Q_DECLARE_METATYPE(TreeData)

class TreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    TreeModel(QObject* parent);
    virtual ~TreeModel();

    enum Columns
    {
        LocationColumn,
        PeakColumn,
        LeakedColumn,
        AllocationsColumn,
        TemporaryColumn,
        NUM_COLUMNS
    };

    enum Roles
    {
        SortRole = Qt::UserRole,
        MaxCostRole,
        SymbolRole,
        ResultDataRole,
    };

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

public slots:
    void resetData(const TreeData& data);
    void setSummary(const SummaryData& data);
    void clearData();

private:
    /// @return the row resembled by @p index
    const RowData* toRow(const QModelIndex& index) const;
    /// @return the row number of @p row in its parent
    int rowOf(const RowData* row) const;

    TreeData m_data;
    RowData m_maxCost;
};

#endif // TREEMODEL_H
