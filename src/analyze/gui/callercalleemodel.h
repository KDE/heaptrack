/*
    SPDX-FileCopyrightText: 2016-2019 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef CALLERCALLEEMODEL_H
#define CALLERCALLEEMODEL_H

#include <QAbstractItemModel>
#include <QVector>

#include <KLocalizedString>

#include "../allocationdata.h"
#include "hashmodel.h"
#include "locationdata.h"
#include "resultdata.h"
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
    std::shared_ptr<const ResultData> resultData;
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
    const CallerCalleeResults& results() const
    {
        return m_results;
    }
    void clearData();

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
        CalleesRole,
        CallersRole,
        SourceMapRole,
        SymbolRole,
        ResultDataRole,
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

    void setResults(const SymbolCostMap& map, std::shared_ptr<const ResultData> resultData)
    {
        Q_ASSERT(resultData);
        m_resultData = std::move(resultData);
        HashModel<SymbolCostMap, ModelImpl>::setRows(map);
    }

    enum Columns
    {
        LocationColumn = 0,
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
        SymbolRole
    };

    QVariant headerCell(int column, int role) const final override
    {
        if (role == Qt::InitialSortOrderRole && column != LocationColumn) {
            return Qt::DescendingOrder;
        } else if (role == Qt::DisplayRole) {
            switch (static_cast<Columns>(column)) {
            case LocationColumn:
                return symbolHeader();
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
                return QVariant::fromValue(symbol);
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
            case NUM_COLUMNS:
                break;
            }
        } else if (role == Qt::DisplayRole) {
            switch (static_cast<Columns>(column)) {
            case LocationColumn:
                return Util::toString(symbol, *m_resultData, Util::Short);
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
            return Util::formatTooltip(symbol, costs, *m_resultData);
        }

        return {};
    }

    int numColumns() const final override
    {
        return NUM_COLUMNS;
    }

private:
    virtual QString symbolHeader() const = 0;

    std::shared_ptr<const ResultData> m_resultData;
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

    void setResults(const LocationCostMap& map, std::shared_ptr<const ResultData> resultData)
    {
        m_resultData = std::move(resultData);
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
        ResultDataRole,
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
                return Util::toString(location, *m_resultData, Util::Long);
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
                return QVariant::fromValue<qint64>(m_resultData->totalCosts().allocations);
            case SelfTemporaryColumn:
            case InclusiveTemporaryColumn:
                return QVariant::fromValue<qint64>(m_resultData->totalCosts().temporary);
            case SelfPeakColumn:
            case InclusivePeakColumn:
                return QVariant::fromValue<qint64>(m_resultData->totalCosts().peak);
            case SelfLeakedColumn:
            case InclusiveLeakedColumn:
                return QVariant::fromValue<qint64>(m_resultData->totalCosts().leaked);
            case LocationColumn:
            case NUM_COLUMNS:
                break;
            }
        } else if (role == Qt::DisplayRole) {
            switch (static_cast<Columns>(column)) {
            case LocationColumn:
                return Util::toString(location, *m_resultData, Util::Short);
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
        } else if (role == ResultDataRole) {
            return QVariant::fromValue(m_resultData.get());
        } else if (role == Qt::ToolTipRole) {
            return Util::formatTooltip(location, costs.selfCost, costs.inclusiveCost, *m_resultData);
        }

        return {};
    }

    int numColumns() const final override
    {
        return NUM_COLUMNS;
    }

private:
    std::shared_ptr<const ResultData> m_resultData;
};

class SourceMapModel : public LocationCostModelImpl<SourceMapModel>
{
    Q_OBJECT
public:
    explicit SourceMapModel(QObject* parent = nullptr);
    ~SourceMapModel();
};

#endif // CALLERCALLEEMODEL_H
