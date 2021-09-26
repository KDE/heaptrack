/*
    SPDX-FileCopyrightText: 2016-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef TOPPROXY_H
#define TOPPROXY_H

#include <QSortFilterProxyModel>

#include "treemodel.h"

class TopProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    enum Type
    {
        Peak,
        Leaked,
        Allocations,
        Temporary
    };

    explicit TopProxy(Type type, QObject* parent = nullptr);
    ~TopProxy() override;

    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
    bool filterAcceptsColumn(int source_column, const QModelIndex& source_parent) const override;
    void setSourceModel(QAbstractItemModel* sourceModel) override;

private:
    void updateCostThreshold();

    Type m_type;
    qint64 m_costThreshold = 0;
};

#endif // TOPPROXY_H
