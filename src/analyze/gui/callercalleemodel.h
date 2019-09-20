/*
 * Copyright 2016-2019 Milian Wolff <mail@milianw.de>
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

#ifndef CALLERCALLEEMODEL_H
#define CALLERCALLEEMODEL_H

#include <QAbstractItemModel>
#include <QVector>

#include "../allocationdata.h"
#include "hashmodel.h"
#include "locationdata.h"
#include "summarydata.h"
#include "util.h"

using SymbolCostMap = QHash<Symbol, AllocationData>;
Q_DECLARE_METATYPE(SymbolCostMap)

using CalleeMap = SymbolCostMap;
using CallerMap = SymbolCostMap;

struct EntryCost
{
    AllocationData inclusiveCost;
    AllocationData selfCost;
};
Q_DECLARE_TYPEINFO(EntryCost, Q_MOVABLE_TYPE);

using LocationCostMap = QHash<FileLine, EntryCost>;
Q_DECLARE_METATYPE(LocationCostMap)

struct CallerCalleeEntry : EntryCost
{
    EntryCost& source(const FileLine& location)
    {
        auto it = sourceMap.find(location);
        if (it == sourceMap.end()) {
            it = sourceMap.insert(location, {});
        }
        return *it;
    }

    AllocationData& callee(const Symbol& symbol)
    {
        auto it = callees.find(symbol);
        if (it == callees.end()) {
            it = callees.insert(symbol, {});
        }
        return *it;
    }

    AllocationData& caller(const Symbol& symbol)
    {
        auto it = callers.find(symbol);
        if (it == callers.end()) {
            it = callers.insert(symbol, {});
        }
        return *it;
    }

    // callers, i.e. other symbols and locations that called this symbol
    CallerMap callers;
    // callees, i.e. symbols being called from this symbol
    CalleeMap callees;
    // source map for this symbol, i.e. locations mapped to associated costs
    LocationCostMap sourceMap;
};

using CallerCalleeEntryMap = QHash<Symbol, CallerCalleeEntry>;
struct CallerCalleeResults
{
    CallerCalleeEntryMap entries;

    CallerCalleeEntry& entry(const Symbol& symbol)
    {
        auto it = entries.find(symbol);
        if (it == entries.end()) {
            it = entries.insert(symbol, {});
        }
        return *it;
    }

    AllocationData totalCosts;
};
Q_DECLARE_TYPEINFO(CallerCalleeResults, Q_MOVABLE_TYPE);
Q_DECLARE_METATYPE(CallerCalleeResults)

class CallerCalleeModel : public HashModel<CallerCalleeEntryMap, CallerCalleeModel>
{
    Q_OBJECT
public:
    explicit CallerCalleeModel(QObject* parent = nullptr);
    ~CallerCalleeModel();

    void setResults(const CallerCalleeResults& results);
    void clearData();

    enum Columns
    {
        SymbolColumn = 0,
        BinaryColumn,
        LocationColumn,
        InclusivePeakColumn,
        InclusiveLeakedColumn,
        InclusiveAllocationsColumn,
        InclusiveTemporaryColumn,
        SelfPeakColumn,
        SelfLeakedColumn,
        SelfAllocationsColumn,
        SelfTemporaryColumn,
        NUM_COLUMNS
    };
    enum
    {
        InitialSortColumn = InclusivePeakColumn
    };

    enum Roles
    {
        SortRole = Qt::UserRole,
        TotalCostRole,
        FilterRole,
        CalleesRole,
        CallersRole,
        SourceMapRole,
        SymbolRole,
        TotalCostsRole,
    };

    QVariant headerCell(int column, int role) const final override;
    QVariant cell(int column, int role, const Symbol& symbol, const CallerCalleeEntry& entry) const final override;
    int numColumns() const final override;
    QModelIndex indexForSymbol(const Symbol& symbol) const;

private:
    CallerCalleeResults m_results;
};

template <typename ModelImpl>
class SymbolCostModelImpl : public HashModel<SymbolCostMap, ModelImpl>
{
public:
    explicit SymbolCostModelImpl(QObject* parent = nullptr)
        : HashModel<SymbolCostMap, ModelImpl>(parent)
    {
    }

    virtual ~SymbolCostModelImpl() = default;

    void setResults(const SymbolCostMap& map, const AllocationData& totalCost)
    {
        m_totalCosts = totalCost;
        HashModel<SymbolCostMap, ModelImpl>::setRows(map);
    }

    enum Columns
    {
        SymbolColumn = 0,
        BinaryColumn,
        LocationColumn,
        PeakColumn,
        LeakedColumn,
        AllocationsColumn,
        TemporaryColumn,
        NUM_COLUMNS
    };
    enum
    {
        InitialSortColumn = PeakColumn
    };

    enum Roles
    {
        SortRole = Qt::UserRole,
        TotalCostRole,
        FilterRole,
        SymbolRole
    };

    QVariant headerCell(int column, int role) const final override
    {
        if (role == Qt::InitialSortOrderRole && column > BinaryColumn) {
            return Qt::DescendingOrder;
        } else if (role == Qt::DisplayRole) {
            switch (static_cast<Columns>(column)) {
            case LocationColumn:
            case SymbolColumn:
                return symbolHeader();
            case BinaryColumn:
                return i18n("Binary");
            case PeakColumn:
                return i18n("Peak");
            case LeakedColumn:
                return i18n("Leaked");
            case AllocationsColumn:
                return i18n("Allocations");
            case TemporaryColumn:
                return i18n("Temporary");
            case NUM_COLUMNS:
                break;
            }
        } else if (role == Qt::ToolTipRole) {
            switch (static_cast<Columns>(column)) {
            case LocationColumn:
                return i18n(
                    "The location of the %1. The function name may be unresolved when debug information is missing.",
                    symbolHeader());
            case SymbolColumn:
                return i18n("The function name of the %1. May be unresolved when debug information is missing.",
                            symbolHeader());
            case BinaryColumn:
                return i18n("The name of the executable the symbol resides in.");
            case PeakColumn:
                return i18n("<qt>The inclusive maximum heap memory in bytes consumed "
                            "from allocations originating at this "
                            "location or from functions called from here. "
                            "This takes deallocations into account.</qt>");
            case LeakedColumn:
                return i18n("<qt>The bytes allocated at this location that have not been "
                            "deallocated.</qt>");
            case AllocationsColumn:
                return i18n("<qt>The inclusive number of times an allocation function "
                            "was called from this location or any "
                            "functions called from here.</qt>");
            case TemporaryColumn:
                return i18n("<qt>The number of inclusive temporary allocations. These "
                            "allocations are directly followed by "
                            "a free without any other allocations in-between.</qt>");
            case NUM_COLUMNS:
                break;
            }
        }

        return {};
    }

    QVariant cell(int column, int role, const Symbol& symbol, const AllocationData& costs) const final override
    {
        if (role == SortRole) {
            switch (static_cast<Columns>(column)) {
            case LocationColumn:
            case SymbolColumn:
                return symbol.symbol;
            case BinaryColumn:
                return symbol.binary;
            case PeakColumn:
                // NOTE: we sort by unsigned absolute value
                return QVariant::fromValue<quint64>(std::abs(costs.peak));
            case LeakedColumn:
                return QVariant::fromValue<quint64>(std::abs(costs.leaked));
            case AllocationsColumn:
                return QVariant::fromValue<quint64>(std::abs(costs.allocations));
            case TemporaryColumn:
                return QVariant::fromValue<quint64>(std::abs(costs.temporary));
            case NUM_COLUMNS:
                break;
            }
        } else if (role == TotalCostRole && column >= PeakColumn) {
            switch (static_cast<Columns>(column)) {
            case PeakColumn:
                return QVariant::fromValue<qint64>(costs.peak);
            case LeakedColumn:
                return QVariant::fromValue<qint64>(costs.leaked);
            case AllocationsColumn:
                return QVariant::fromValue<qint64>(costs.allocations);
            case TemporaryColumn:
                return QVariant::fromValue<qint64>(costs.temporary);
            case LocationColumn:
            case SymbolColumn:
            case BinaryColumn:
            case NUM_COLUMNS:
                break;
            }
        } else if (role == FilterRole) {
            // TODO: optimize this
            return QString(symbol.symbol + symbol.binary);
        } else if (role == Qt::DisplayRole) {
            switch (static_cast<Columns>(column)) {
            case LocationColumn:
                return i18nc("%1: function name, %2: binary basename", "%1 in %2", symbol.symbol, symbol.binary);
            case SymbolColumn:
                return symbol.symbol;
            case BinaryColumn:
                return symbol.binary;
            case PeakColumn:
                return Util::formatBytes(costs.peak);
            case LeakedColumn:
                return Util::formatBytes(costs.leaked);
            case AllocationsColumn:
                return QVariant::fromValue<qint64>(costs.allocations);
            case TemporaryColumn:
                return QVariant::fromValue<qint64>(costs.temporary);
            case NUM_COLUMNS:
                break;
            }
        } else if (role == SymbolRole) {
            return QVariant::fromValue(symbol);
        } else if (role == Qt::ToolTipRole) {
            return Util::formatTooltip(symbol, costs, m_totalCosts);
        }

        return {};
    }

    int numColumns() const final override
    {
        return NUM_COLUMNS;
    }

private:
    virtual QString symbolHeader() const = 0;

    AllocationData m_totalCosts;
};

class CallerModel : public SymbolCostModelImpl<CallerModel>
{
    Q_OBJECT
public:
    explicit CallerModel(QObject* parent = nullptr);
    ~CallerModel();

    QString symbolHeader() const final override;
};

class CalleeModel : public SymbolCostModelImpl<CalleeModel>
{
    Q_OBJECT
public:
    explicit CalleeModel(QObject* parent = nullptr);
    ~CalleeModel();

    QString symbolHeader() const final override;
};

template <typename ModelImpl>
class LocationCostModelImpl : public HashModel<LocationCostMap, ModelImpl>
{
public:
    explicit LocationCostModelImpl(QObject* parent = nullptr)
        : HashModel<LocationCostMap, ModelImpl>(parent)
    {
    }

    virtual ~LocationCostModelImpl() = default;

    void setResults(const LocationCostMap& map, const AllocationData& totalCosts)
    {
        m_totalCosts = totalCosts;
        HashModel<LocationCostMap, ModelImpl>::setRows(map);
    }

    enum Columns
    {
        LocationColumn = 0,
        InclusivePeakColumn,
        InclusiveLeakedColumn,
        InclusiveAllocationsColumn,
        InclusiveTemporaryColumn,
        SelfPeakColumn,
        SelfLeakedColumn,
        SelfAllocationsColumn,
        SelfTemporaryColumn,
        NUM_COLUMNS
    };
    enum
    {
        InitialSortColumn = InclusivePeakColumn
    };

    enum Roles
    {
        SortRole = Qt::UserRole,
        TotalCostRole,
        FilterRole,
        LocationRole
    };

    QVariant headerCell(int column, int role) const final override
    {
        if (role == Qt::InitialSortOrderRole && column > LocationColumn) {
            return Qt::DescendingOrder;
        } else if (role == Qt::DisplayRole) {
            switch (static_cast<Columns>(column)) {
            case LocationColumn:
                return i18n("Location");
            case SelfAllocationsColumn:
                return i18n("Allocations (Self)");
            case SelfTemporaryColumn:
                return i18n("Temporary (Self)");
            case SelfPeakColumn:
                return i18n("Peak (Self)");
            case SelfLeakedColumn:
                return i18n("Leaked (Self)");
            case InclusiveAllocationsColumn:
                return i18n("Allocations (Incl.)");
            case InclusiveTemporaryColumn:
                return i18n("Temporary (Incl.)");
            case InclusivePeakColumn:
                return i18n("Peak (Incl.)");
            case InclusiveLeakedColumn:
                return i18n("Leaked (Incl.)");
            case NUM_COLUMNS:
                break;
            }
        } else if (role == Qt::ToolTipRole) {
            switch (static_cast<Columns>(column)) {
            case LocationColumn:
                return i18n("<qt>The source code location that called an allocation function. "
                            "May be unknown when debug information is missing.</qt>");
            case SelfAllocationsColumn:
                return i18n("<qt>The number of times an allocation function was directly "
                            "called from this location.</qt>");
            case SelfTemporaryColumn:
                return i18n("<qt>The number of direct temporary allocations. These "
                            "allocations are directly followed by a "
                            "free without any other allocations in-between.</qt>");
            case SelfPeakColumn:
                return i18n("<qt>The maximum heap memory in bytes consumed from "
                            "allocations originating directly at "
                            "this location. "
                            "This takes deallocations into account.</qt>");
            case SelfLeakedColumn:
                return i18n("<qt>The bytes allocated directly at this location that have "
                            "not been deallocated.</qt>");
            case InclusiveAllocationsColumn:
                return i18n("<qt>The inclusive number of times an allocation function "
                            "was called from this location or any "
                            "functions called from here.</qt>");
            case InclusiveTemporaryColumn:
                return i18n("<qt>The number of inclusive temporary allocations. These "
                            "allocations are directly followed by "
                            "a free without any other allocations in-between.</qt>");
            case InclusivePeakColumn:
                return i18n("<qt>The inclusive maximum heap memory in bytes consumed "
                            "from allocations originating at this "
                            "location or from functions called from here. "
                            "This takes deallocations into account.</qt>");
            case InclusiveLeakedColumn:
                return i18n("<qt>The bytes allocated at this location that have not been "
                            "deallocated.</qt>");
            case NUM_COLUMNS:
                break;
            }
        }

        return {};
    }

    QVariant cell(int column, int role, const FileLine& location, const EntryCost& costs) const final override
    {
        if (role == SortRole) {
            switch (static_cast<Columns>(column)) {
            case LocationColumn:
                return location.toString();
            case SelfAllocationsColumn:
                // NOTE: we sort by unsigned absolute value
                return QVariant::fromValue<quint64>(std::abs(costs.selfCost.allocations));
            case SelfTemporaryColumn:
                return QVariant::fromValue<quint64>(std::abs(costs.selfCost.temporary));
            case SelfPeakColumn:
                return QVariant::fromValue<quint64>(std::abs(costs.selfCost.peak));
            case SelfLeakedColumn:
                return QVariant::fromValue<quint64>(std::abs(costs.selfCost.leaked));
            case InclusiveAllocationsColumn:
                return QVariant::fromValue<quint64>(std::abs(costs.inclusiveCost.allocations));
            case InclusiveTemporaryColumn:
                return QVariant::fromValue<quint64>(std::abs(costs.inclusiveCost.temporary));
            case InclusivePeakColumn:
                return QVariant::fromValue<quint64>(std::abs(costs.inclusiveCost.peak));
            case InclusiveLeakedColumn:
                return QVariant::fromValue<quint64>(std::abs(costs.inclusiveCost.leaked));
            case NUM_COLUMNS:
                break;
            }
        } else if (role == TotalCostRole) {
            switch (static_cast<Columns>(column)) {
            case SelfAllocationsColumn:
            case InclusiveAllocationsColumn:
                return QVariant::fromValue<qint64>(m_totalCosts.allocations);
            case SelfTemporaryColumn:
            case InclusiveTemporaryColumn:
                return QVariant::fromValue<qint64>(m_totalCosts.temporary);
            case SelfPeakColumn:
            case InclusivePeakColumn:
                return QVariant::fromValue<qint64>(m_totalCosts.peak);
            case SelfLeakedColumn:
            case InclusiveLeakedColumn:
                return QVariant::fromValue<qint64>(m_totalCosts.leaked);
            case LocationColumn:
            case NUM_COLUMNS:
                break;
            }
        } else if (role == FilterRole) {
            return location.toString();
        } else if (role == Qt::DisplayRole) {
            switch (static_cast<Columns>(column)) {
            case LocationColumn:
                return Util::basename(location.toString());
            case SelfAllocationsColumn:
                return QVariant::fromValue<qint64>(costs.selfCost.allocations);
            case SelfTemporaryColumn:
                return QVariant::fromValue<qint64>(costs.selfCost.temporary);
            case SelfPeakColumn:
                return Util::formatBytes(costs.selfCost.peak);
            case SelfLeakedColumn:
                return Util::formatBytes(costs.selfCost.leaked);
            case InclusiveAllocationsColumn:
                return QVariant::fromValue<qint64>(costs.inclusiveCost.allocations);
            case InclusiveTemporaryColumn:
                return QVariant::fromValue<qint64>(costs.inclusiveCost.temporary);
            case InclusivePeakColumn:
                return Util::formatBytes(costs.inclusiveCost.peak);
            case InclusiveLeakedColumn:
                return Util::formatBytes(costs.inclusiveCost.leaked);
            case NUM_COLUMNS:
                break;
            }
        } else if (role == LocationRole) {
            return QVariant::fromValue(location);
        } else if (role == Qt::ToolTipRole) {
            return Util::formatTooltip(location, costs.selfCost, costs.inclusiveCost, m_totalCosts);
        }

        return {};
    }

    int numColumns() const final override
    {
        return NUM_COLUMNS;
    }

private:
    AllocationData m_totalCosts;
};

class SourceMapModel : public LocationCostModelImpl<SourceMapModel>
{
    Q_OBJECT
public:
    explicit SourceMapModel(QObject* parent = nullptr);
    ~SourceMapModel();
};

#endif // CALLERCALLEEMODEL_H
