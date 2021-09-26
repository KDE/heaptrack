/*
    SPDX-FileCopyrightText: 2016-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef SUMMARYDATA_H
#define SUMMARYDATA_H

#include "../allocationdata.h"
#include "../filterparameters.h"
#include "../suppressions.h"

#include <QMetaType>
#include <QString>
#include <QVector>

struct SummaryData
{
    SummaryData() = default;
    SummaryData(const QString& debuggee, const AllocationData& cost, int64_t totalTime,
                const FilterParameters& filterParameters, int64_t peakTime, int64_t peakRSS, int64_t totalSystemMemory,
                bool fromAttached, int64_t totalLeakedSuppressed, QVector<Suppression> suppressions)
        : debuggee(debuggee)
        , cost(cost)
        , totalLeakedSuppressed(totalLeakedSuppressed)
        , totalTime(totalTime)
        , filterParameters(filterParameters)
        , peakTime(peakTime)
        , peakRSS(peakRSS)
        , totalSystemMemory(totalSystemMemory)
        , fromAttached(fromAttached)
        , suppressions(std::move(suppressions))
    {
    }
    QString debuggee;
    AllocationData cost;
    int64_t totalLeakedSuppressed = 0;
    int64_t totalTime = 0;
    FilterParameters filterParameters;
    int64_t peakTime = 0;
    int64_t peakRSS = 0;
    int64_t totalSystemMemory = 0;
    bool fromAttached = false;
    QVector<Suppression> suppressions;
};
Q_DECLARE_METATYPE(SummaryData)

#endif // SUMMARYDATA_H
