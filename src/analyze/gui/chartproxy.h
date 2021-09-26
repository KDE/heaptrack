/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef CHARTPROXY_H
#define CHARTPROXY_H

#include <QSortFilterProxyModel>

#include "chartmodel.h"

class ChartProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit ChartProxy(bool showTotal, QObject* parent = nullptr);
    virtual ~ChartProxy();

protected:
    bool filterAcceptsColumn(int sourceColumn, const QModelIndex& sourceParent) const override;

private:
    bool m_showTotal;
};

#endif // CHARTPROXY_H
