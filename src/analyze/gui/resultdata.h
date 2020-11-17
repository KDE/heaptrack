/*
 * Copyright 2020 Milian Wolff <mail@milianw.de>
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
