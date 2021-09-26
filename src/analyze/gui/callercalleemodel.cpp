/*
    SPDX-FileCopyrightText: 2016-2019 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "callercalleemodel.h"

#include <KLocalizedString>

CallerCalleeModel::CallerCalleeModel(QObject* parent)
    : HashModel(parent)
{
    qRegisterMetaType<CallerCalleeResults>();
}

CallerCalleeModel::~CallerCalleeModel() = default;

void CallerCalleeModel::setResults(const CallerCalleeResults& results)
{
    Q_ASSERT(results.resultData);
    m_results = results;
    setRows(results.entries);
}

void CallerCalleeModel::clearData()
{
    m_results = {};
    setRows({});
}

QVariant CallerCalleeModel::headerCell(int column, int role) const
{
    if (role == Qt::InitialSortOrderRole) {
        return (column > LocationColumn) ? Qt::DescendingOrder : Qt::AscendingOrder;
    }
    if (role == Qt::DisplayRole) {
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
            return i18n("<qt>The parent symbol that called an allocation function. "
                        "The function name may be unresolved when debug information is missing.</qt>");
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

QVariant CallerCalleeModel::cell(int column, int role, const Symbol& symbol, const CallerCalleeEntry& entry) const
{
    if (role == SymbolRole) {
        return QVariant::fromValue(symbol);
    } else if (role == SortRole) {
        switch (static_cast<Columns>(column)) {
        case LocationColumn:
            return Util::toString(symbol, *m_results.resultData, Util::Short);
        case SelfAllocationsColumn:
            // NOTE: we sort by unsigned absolute value
            return QVariant::fromValue<quint64>(std::abs(entry.selfCost.allocations));
        case SelfTemporaryColumn:
            return QVariant::fromValue<quint64>(std::abs(entry.selfCost.temporary));
        case SelfPeakColumn:
            return QVariant::fromValue<quint64>(std::abs(entry.selfCost.peak));
        case SelfLeakedColumn:
            return QVariant::fromValue<quint64>(std::abs(entry.selfCost.leaked));
        case InclusiveAllocationsColumn:
            return QVariant::fromValue<quint64>(std::abs(entry.inclusiveCost.allocations));
        case InclusiveTemporaryColumn:
            return QVariant::fromValue<quint64>(std::abs(entry.inclusiveCost.temporary));
        case InclusivePeakColumn:
            return QVariant::fromValue<quint64>(std::abs(entry.inclusiveCost.peak));
        case InclusiveLeakedColumn:
            return QVariant::fromValue<quint64>(std::abs(entry.inclusiveCost.leaked));
        case NUM_COLUMNS:
            break;
        }
    } else if (role == TotalCostRole) {
        const auto& totalCosts = m_results.resultData->totalCosts();
        switch (static_cast<Columns>(column)) {
        case SelfAllocationsColumn:
        case InclusiveAllocationsColumn:
            return QVariant::fromValue<qint64>(totalCosts.allocations);
        case SelfTemporaryColumn:
        case InclusiveTemporaryColumn:
            return QVariant::fromValue<qint64>(totalCosts.temporary);
        case SelfPeakColumn:
        case InclusivePeakColumn:
            return QVariant::fromValue<qint64>(totalCosts.peak);
        case SelfLeakedColumn:
        case InclusiveLeakedColumn:
            return QVariant::fromValue<qint64>(totalCosts.leaked);
        case LocationColumn:
        case NUM_COLUMNS:
            break;
        }
    } else if (role == Qt::DisplayRole) {
        switch (static_cast<Columns>(column)) {
        case LocationColumn:
            return Util::toString(symbol, *m_results.resultData, Util::Short);
        case SelfAllocationsColumn:
            return QVariant::fromValue<qint64>(entry.selfCost.allocations);
        case SelfTemporaryColumn:
            return QVariant::fromValue<qint64>(entry.selfCost.temporary);
        case SelfPeakColumn:
            return Util::formatBytes(entry.selfCost.peak);
        case SelfLeakedColumn:
            return Util::formatBytes(entry.selfCost.leaked);
        case InclusiveAllocationsColumn:
            return QVariant::fromValue<qint64>(entry.inclusiveCost.allocations);
        case InclusiveTemporaryColumn:
            return QVariant::fromValue<qint64>(entry.inclusiveCost.temporary);
        case InclusivePeakColumn:
            return Util::formatBytes(entry.inclusiveCost.peak);
        case InclusiveLeakedColumn:
            return Util::formatBytes(entry.inclusiveCost.leaked);
        case NUM_COLUMNS:
            break;
        }
    } else if (role == CalleesRole) {
        return QVariant::fromValue(entry.callees);
    } else if (role == CallersRole) {
        return QVariant::fromValue(entry.callers);
    } else if (role == SourceMapRole) {
        return QVariant::fromValue(entry.sourceMap);
    } else if (role == Qt::ToolTipRole) {
        return Util::formatTooltip(symbol, entry.selfCost, entry.inclusiveCost, *m_results.resultData);
    } else if (role == ResultDataRole) {
        return QVariant::fromValue(m_results.resultData.get());
    }

    return {};
}

QModelIndex CallerCalleeModel::indexForSymbol(const Symbol& symbol) const
{
    return indexForKey(symbol);
}

CallerModel::CallerModel(QObject* parent)
    : SymbolCostModelImpl(parent)
{
}

CallerModel::~CallerModel() = default;

QString CallerModel::symbolHeader() const
{
    return i18n("Caller");
}

CalleeModel::CalleeModel(QObject* parent)
    : SymbolCostModelImpl(parent)
{
}

CalleeModel::~CalleeModel() = default;

QString CalleeModel::symbolHeader() const
{
    return i18n("Callee");
}

SourceMapModel::SourceMapModel(QObject* parent)
    : LocationCostModelImpl(parent)
{
}

SourceMapModel::~SourceMapModel() = default;

int CallerCalleeModel::numColumns() const
{
    return NUM_COLUMNS;
}
