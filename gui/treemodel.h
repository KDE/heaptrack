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

#ifndef TREEMODEL_H
#define TREEMODEL_H

#include <QAbstractItemModel>
#include <QVector>

#include <KFormat>

#include <boost/functional/hash.hpp>

#include <memory>

struct SummaryData
{
    QString debuggee;
    uint64_t totalTime;
    uint64_t peakTime;
    uint64_t peak;
    uint64_t leaked;
    uint64_t allocations;
    uint64_t temporary;
    uint64_t allocated;
    uint64_t peakRSS;
    uint64_t totalSystemMemory;
};
Q_DECLARE_METATYPE(SummaryData);

struct LocationData
{
    QString function;
    QString file;
    QString module;
    int line;

    bool operator==(const LocationData& rhs) const
    {
        return function == rhs.function
            && file == rhs.file
            && module == rhs.module
            && line == rhs.line;
    }

    bool operator<(const LocationData& rhs) const
    {
        int i = function.compare(rhs.function);
        if (!i) {
            i = file.compare(rhs.file);
        }
        if (!i) {
            i = line < rhs.line ? -1 : (line > rhs.line);
        }
        if (!i) {
            i = module.compare(rhs.module);
        }
        return i < 0;
    }
};
Q_DECLARE_TYPEINFO(LocationData, Q_MOVABLE_TYPE);

inline bool operator<(const std::shared_ptr<LocationData>& lhs, const LocationData& rhs)
{
    return *lhs < rhs;
}

struct RowData
{
    quint64 allocations;
    quint64 peak;
    quint64 leaked;
    quint64 allocated;
    quint64 temporary;
    std::shared_ptr<LocationData> location;
    const RowData* parent;
    QVector<RowData> children;
    bool operator<(const std::shared_ptr<LocationData>& rhs) const
    {
        return *location < *rhs;
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

    enum Columns {
        AllocationsColumn,
        TemporaryColumn,
        PeakColumn,
        LeakedColumn,
        AllocatedColumn,
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

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

public slots:
    void resetData(const TreeData& data);
    void setSummary(const SummaryData& data);

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

