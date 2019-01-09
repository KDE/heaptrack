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

#include <KFormat>
#include <KLocalizedString>

QString Util::basename(const QString& path)
{
    int idx = path.lastIndexOf(QLatin1Char('/'));
    return path.mid(idx + 1);
}

QString Util::formatString(const QString& input)
{
    return input.isEmpty() ? i18n("??") : input;
}

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

QString Util::formatBytes(qint64 bytes)
{
    static const KFormat format;
    return format.formatByteSize(bytes, 1, KFormat::MetricBinaryDialect);
}

QString Util::formatCostRelative(qint64 selfCost, qint64 totalCost, bool addPercentSign)
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

QString Util::formatTooltip(const Symbol& symbol, const AllocationData& costs, const AllocationData& totalCosts)
{
    auto toolTip = i18n("symbol: <tt>%1</tt><br/>binary: <tt>%2 (%3)</tt>", symbol.symbol.toHtmlEscaped(),
                        symbol.binary.toHtmlEscaped(), symbol.path.toHtmlEscaped());

    auto formatCost = [&](const QString& label, int64_t AllocationData::*member) -> QString {
        const auto cost = costs.*member;
        const auto total = totalCosts.*member;
        if (!total) {
            return QString();
        }

        return QLatin1String("<hr/>")
            + i18n("%1: %2<br/>&nbsp;&nbsp;%4% out of %3 total", label, cost, total,
                   Util::formatCostRelative(cost, total));
    };

    toolTip += formatCost(i18n("Peak"), &AllocationData::peak);
    toolTip += formatCost(i18n("Leaked"), &AllocationData::leaked);
    toolTip += formatCost(i18n("Allocations"), &AllocationData::allocations);
    toolTip += formatCost(i18n("Temporary Allocations"), &AllocationData::temporary);
    return QString(QLatin1String("<qt>") + toolTip + QLatin1String("</qt>"));
}

QString Util::formatTooltip(const Symbol& symbol, const AllocationData& selfCosts, const AllocationData& inclusiveCosts,
                            const AllocationData& totalCosts)
{
    auto toolTip = i18n("symbol: <tt>%1</tt><br/>binary: <tt>%2 (%3)</tt>", symbol.symbol.toHtmlEscaped(),
                        symbol.binary.toHtmlEscaped(), symbol.path.toHtmlEscaped());

    auto formatCost = [&](const QString& label, int64_t AllocationData::*member) -> QString {
        const auto selfCost = selfCosts.*member;
        const auto inclusiveCost = inclusiveCosts.*member;
        const auto total = totalCosts.*member;
        if (!total) {
            return QString();
        }

        return QLatin1String("<hr/>")
            + i18n("%1 (self): %2<br/>&nbsp;&nbsp;%4% out of %3 total", label, selfCost, total,
                   Util::formatCostRelative(selfCost, total))
            + QLatin1String("<br/>")
            + i18n("%1 (inclusive): %2<br/>&nbsp;&nbsp;%4% out of %3 total", label, inclusiveCost, total,
                   Util::formatCostRelative(inclusiveCost, total));
    };

    toolTip += formatCost(i18n("Peak"), &AllocationData::peak);
    toolTip += formatCost(i18n("Leaked"), &AllocationData::leaked);
    toolTip += formatCost(i18n("Allocations"), &AllocationData::allocations);
    toolTip += formatCost(i18n("Temporary Allocations"), &AllocationData::temporary);
    return QString(QLatin1String("<qt>") + toolTip + QLatin1String("</qt>"));
}

QString Util::formatTooltip(const FileLine& location, const AllocationData& selfCosts,
                            const AllocationData& inclusiveCosts, const AllocationData& totalCosts)
{
    QString toolTip = location.toString().toHtmlEscaped();

    auto formatCost = [&](const QString& label, int64_t AllocationData::*member) -> QString {
        const auto selfCost = selfCosts.*member;
        const auto inclusiveCost = inclusiveCosts.*member;
        const auto total = totalCosts.*member;
        if (!total) {
            return QString();
        }

        return QLatin1String("<hr/>")
            + i18n("%1 (self): %2<br/>&nbsp;&nbsp;%4% out of %3 total", label, selfCost, total,
                   Util::formatCostRelative(selfCost, total))
            + QLatin1String("<br/>")
            + i18n("%1 (inclusive): %2<br/>&nbsp;&nbsp;%4% out of %3 total", label, inclusiveCost, total,
                   Util::formatCostRelative(inclusiveCost, total));
    };

    toolTip += formatCost(i18n("Peak"), &AllocationData::peak);
    toolTip += formatCost(i18n("Leaked"), &AllocationData::leaked);
    toolTip += formatCost(i18n("Allocations"), &AllocationData::allocations);
    toolTip += formatCost(i18n("Temporary Allocations"), &AllocationData::temporary);
    return QString(QLatin1String("<qt>") + toolTip + QLatin1String("</qt>"));
}
