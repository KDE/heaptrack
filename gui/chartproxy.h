/*
 * Copyright 2015 Milian Wolff <mail@milianw.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef CHARTPROXY_H
#define CHARTPROXY_H

#include <QSortFilterProxyModel>
#include "chartmodel.h"

class ChartProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit ChartProxy(ChartModel::Columns column, QObject* parent = nullptr);
    virtual ~ChartProxy();

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex & proxyIndex, int role = Qt::DisplayRole) const override;

protected:
    bool filterAcceptsColumn(int sourceColumn, const QModelIndex& sourceParent) const override;

private:
    ChartModel::Columns m_column;
};

#endif //CHARTPROXY_H
