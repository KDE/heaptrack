/*
 * Copyright 2017-2019 Milian Wolff <mail@milianw.de>
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

#ifndef UTIL_H
#define UTIL_H

#include <qglobal.h>

#include "../allocationdata.h"
#include "locationdata.h"

class QString;

namespace Util {
QString basename(const QString& path);
QString formatString(const QString& input);
QString formatTime(qint64 ms);
QString formatBytes(qint64 bytes);
QString formatCostRelative(qint64 selfCost, qint64 totalCost, bool addPercentSign = false);
QString formatTooltip(const Symbol& symbol, const AllocationData& costs, const AllocationData& totalCosts);
QString formatTooltip(const Symbol& symbol, const AllocationData& selfCosts, const AllocationData& inclusiveCosts,
                      const AllocationData& totalCosts);
QString formatTooltip(const FileLine& location, const AllocationData& selfCosts, const AllocationData& inclusiveCosts,
                      const AllocationData& totalCosts);
}

Q_DECLARE_TYPEINFO(AllocationData, Q_MOVABLE_TYPE);
Q_DECLARE_METATYPE(AllocationData)

#endif // UTIL_H
