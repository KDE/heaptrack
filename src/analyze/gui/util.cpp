/*
    SPDX-FileCopyrightText: 2017-2019 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "util.h"

#include <QString>

#include <resultdata.h>

#include <KFormat>
#include <KLocalizedString>

namespace {
const KFormat& format()
{
    static const KFormat format;
    return format;
}
}

QString Util::basename(const QString& path)
{
    int idx = path.lastIndexOf(QLatin1Char('/'));
    return path.mid(idx + 1);
}

QString Util::elideTemplateArguments(const QString& s)
{
    const auto startBracket = QLatin1Char('<');
    const auto stopBracket  = QLatin1Char('>');

    int level = 0;
    QString result;
    result.reserve(s.size());
    for (auto currentChar : s) {
        if (currentChar == startBracket) {
            if (level == 0) {
                result += startBracket;
            }
            ++level;
        } else if (currentChar == stopBracket) {
            if (level == 1) {
                result += stopBracket;
            }
            --level;
        } else if (level == 0) {
            result += currentChar;
        }
    }
    return result;
}

QString Util::formatString(const QString& input)
{
    return input.isEmpty() ? i18n("??") : input;
}

QString Util::formatTime(qint64 ms)
{
    auto format = [](quint64 fragment, int precision) -> QString {
        return QString::number(fragment).rightJustified(precision, QLatin1Char('0'));
    };

    if (std::abs(ms) < 1000) {
        QString ret = QString::number(ms) + QLatin1String("ms");
    }

    const auto isNegative = ms < 0;
    if (isNegative)
        ms = -ms;
    qint64 totalSeconds = ms / 1000;
    ms = ms % 1000;
    qint64 days = totalSeconds / 60 / 60 / 24;
    qint64 hours = (totalSeconds / 60 / 60) % 24;
    qint64 minutes = (totalSeconds / 60) % 60;
    qint64 seconds = totalSeconds % 60;

    auto optional = [](quint64 fragment, const char* unit) -> QString {
        if (fragment > 0)
            return QString::number(fragment) + QLatin1String(unit);
        return QString();
    };

    QString ret = optional(days, "d") + optional(hours, "h") + optional(minutes, "min");
    const auto showMs = ret.isEmpty();
    ret += format(seconds, 2);
    if (showMs)
        ret += QLatin1Char('.') + format(showMs ? ms : 0, 3);
    ret += QLatin1Char('s');
    if (isNegative)
        ret.prepend(QLatin1Char('-'));
    return ret;
}

QString Util::formatBytes(qint64 bytes)
{
    auto ret = format().formatByteSize(bytes, 1, KFormat::MetricBinaryDialect);
    // remove spaces, otherwise HTML might break between the unit and the cost
    // note that we also don't add a space before our time units above
    ret.remove(QLatin1Char(' '));
    return ret;
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

QString Util::formatTooltip(const Symbol& symbol, const AllocationData& costs, const ResultData& resultData)
{
    const auto& totalCosts = resultData.totalCosts();

    auto toolTip = Util::toString(symbol, resultData, Util::Long);

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
                            const ResultData& resultData)
{
    const auto& totalCosts = resultData.totalCosts();
    auto toolTip = Util::toString(symbol, resultData, Util::Long);

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
                            const AllocationData& inclusiveCosts, const ResultData& resultData)
{
    QString toolTip = toString(location, resultData, Util::Long).toHtmlEscaped();
    const auto& totalCosts = resultData.totalCosts();

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

QString Util::toString(const Symbol& symbol, const ResultData& resultData, FormatType formatType)
{
    const auto& binaryPath = resultData.string(symbol.moduleId);
    const auto binaryName = Util::basename(binaryPath);
    switch (formatType) {
    case Long:
        return i18n("symbol: <tt>%1</tt><br/>binary: <tt>%2 (%3)</tt>",
                    resultData.string(symbol.functionId).toHtmlEscaped(), binaryName.toHtmlEscaped(),
                    binaryPath.toHtmlEscaped());
    case Short:
        return i18nc("%1: function name, %2: binary basename", "%1 in %2", resultData.string(symbol.functionId),
                     Util::basename(resultData.string(symbol.moduleId)));
    }
    Q_UNREACHABLE();
}

QString Util::toString(const FileLine& location, const ResultData& resultData, FormatType formatType)
{
    auto file = resultData.string(location.fileId);
    switch (formatType) {
    case Long:
        break;
    case Short:
        file = Util::basename(file);
        break;
    }

    return file.isEmpty() ? QStringLiteral("??") : (file + QLatin1Char(':') + QString::number(location.line));
}

const QString& Util::unresolvedFunctionName()
{
    static QString msg = i18n("<unresolved function>");
    return msg;
}
