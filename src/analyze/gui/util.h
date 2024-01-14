/*
    SPDX-FileCopyrightText: 2017-2019 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef UTIL_H
#define UTIL_H

#include <qglobal.h>

#include "../allocationdata.h"
#include "locationdata.h"

class QString;

class ResultData;

namespace Util {
QString basename(const QString& path);
QString elideTemplateArguments(const QString& s);
QString formatString(const QString& input);
QString formatTime(qint64 ms);
QString formatBytes(qint64 bytes);
QString formatCostRelative(qint64 selfCost, qint64 totalCost, bool addPercentSign = false);
QString formatTooltip(const Symbol& symbol, const AllocationData& costs, const ResultData& resultData);
QString formatTooltip(const Symbol& symbol, const AllocationData& selfCosts, const AllocationData& inclusiveCosts,
                      const ResultData& resultDat);
QString formatTooltip(const FileLine& location, const AllocationData& selfCosts, const AllocationData& inclusiveCosts,
                      const ResultData& resultDat);

enum FormatType
{
    Long,
    Short
};
QString toString(const Symbol& symbol, const ResultData& resultData, FormatType formatType);
QString toString(const FileLine& location, const ResultData& resultData, FormatType formatType);
const QString& unresolvedFunctionName();
}

Q_DECLARE_TYPEINFO(AllocationData, Q_MOVABLE_TYPE);
Q_DECLARE_METATYPE(AllocationData)

#endif // UTIL_H
