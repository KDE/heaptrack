/*
 * Copyright 2016-2017 Milian Wolff <mail@milianw.de>
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

#ifndef CALLERCALLEEMODEL_H
#define CALLERCALLEEMODEL_H

#include <QVector>
#include <QAbstractTableModel>

#include <KFormat>

#include "../allocationdata.h"
#include "locationdata.h"
#include "summarydata.h"

struct CallerCalleeData
{
    AllocationData inclusiveCost;
    AllocationData selfCost;
    std::shared_ptr<LocationData> location;
};
Q_DECLARE_TYPEINFO(CallerCalleeData, Q_MOVABLE_TYPE);

using CallerCalleeRows = QVector<CallerCalleeData>;
Q_DECLARE_METATYPE(CallerCalleeRows)

class CallerCalleeModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit CallerCalleeModel(QObject* parent = nullptr);
    ~CallerCalleeModel();

    enum Columns {
        InclusiveAllocationsColumn,
        InclusiveTemporaryColumn,
        InclusivePeakColumn,
        InclusiveLeakedColumn,
        InclusiveAllocatedColumn,
        SelfAllocationsColumn,
        SelfTemporaryColumn,
        SelfPeakColumn,
        SelfLeakedColumn,
        SelfAllocatedColumn,
        FunctionColumn,
        FileColumn,
        LineColumn,
        ModuleColumn,
        LocationColumn,
        NUM_COLUMNS
    };

    enum Roles {
        SortRole = Qt::UserRole,
        MaxCostRole = Qt::UserRole + 1
    };

    QVariant headerData(int section, Qt::Orientation orientation = Qt::Horizontal,
                        int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    int columnCount(const QModelIndex& parent = {}) const override;
    int rowCount(const QModelIndex &parent = {}) const override;

    void resetData(const QVector<CallerCalleeData>& rows);
    void setSummary(const SummaryData& data);
private:
    QVector<CallerCalleeData> m_rows;
    CallerCalleeData m_maxCost;
    KFormat m_format;
};

#endif // CALLERCALLEEMODEL_H
