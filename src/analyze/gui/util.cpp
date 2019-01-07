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

#include "util.h"

#include <QString>

QString Util::formatTime(qint64 ms)
{
    if (ms > 60000) {
        // minutes
        return QString::number(double(ms) / 60000, 'g', 3) + QLatin1String("min");
    } else {
        // seconds
        return QString::number(double(ms) / 1000, 'g', 3) + QLatin1Char('s');
    }
}

QString Util::formatCostRelative(quint64 selfCost, quint64 totalCost, bool addPercentSign)
{
    if (!totalCost) {
        return QString();
    }

    auto ret = QString::number(static_cast<double>(selfCost) * 100. / totalCost, 'g', 3);
    if (addPercentSign) {
        ret.append(QLatin1Char('%'));
    }
    return ret;
}
