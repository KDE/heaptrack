/*
    SPDX-FileCopyrightText: 2020 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef RESULTDATA_H
#define RESULTDATA_H

#include <analyze/allocationdata.h>

#include "locationdata.h"
#include "util.h"

#include <QVector>

#include <memory>

class ResultData
{
public:
    ResultData(AllocationData totalCosts, QVector<QString> strings)
        : m_totalCosts(std::move(totalCosts))
        , m_strings(std::move(strings))
    {
    }

    QString string(StringIndex stringId) const
    {
        return m_strings.value(stringId.index - 1);
    }

    QString string(FunctionIndex functionIndex) const
    {
        return functionIndex ? string(static_cast<StringIndex>(functionIndex)) : Util::unresolvedFunctionName();
    }

    const AllocationData& totalCosts() const
    {
        return m_totalCosts;
    }

private:
    AllocationData m_totalCosts;
    QVector<QString> m_strings;
};

Q_DECLARE_METATYPE(const ResultData*)

#endif // RESULTDATA_H
