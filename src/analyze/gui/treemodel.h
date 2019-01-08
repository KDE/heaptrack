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

#ifndef TREEMODEL_H
#define TREEMODEL_H

#include <QAbstractItemModel>
#include <QVector>

#include <KFormat>

#include "../allocationdata.h"
#include "locationdata.h"
#include "summarydata.h"

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

using TreeData = QVector<RowData>;
Q_DECLARE_METATYPE(TreeData)

class TreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    TreeModel(QObject* parent);
    virtual ~TreeModel();

    enum Columns
    {
        PeakColumn,
        LeakedColumn,
        AllocationsColumn,
        TemporaryColumn,
        FunctionColumn,
        ModuleColumn,
        LocationColumn,
        NUM_COLUMNS
    };

    enum Roles
    {
        SortRole = Qt::UserRole,
        MaxCostRole,
        SymbolRole
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
    // TODO: update via global event filter when the locale changes (changeEvent)
    KFormat m_format;
};

#endif // TREEMODEL_H
