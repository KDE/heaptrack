/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "chartproxy.h"

#include "chartmodel.h"

ChartProxy::ChartProxy(bool showTotal, QObject* parent)
    : QSortFilterProxyModel(parent)
    , m_showTotal(showTotal)
{
}

ChartProxy::~ChartProxy() = default;

bool ChartProxy::filterAcceptsColumn(int sourceColumn, const QModelIndex& /*sourceParent*/) const
{
    if (m_showTotal && sourceColumn >= 2)
        return false;
    else if (!m_showTotal && sourceColumn < 2)
        return false;
    return true;
}
