/*
 * Copyright 2016-2017 Milian Wolff <mail@milianw.de>
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

#ifndef TOPPROXY_H
#define TOPPROXY_H

#include <QSortFilterProxyModel>

#include "treemodel.h"

class TopProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    enum Type {
        Peak,
        Leaked,
        Allocations,
        Allocated,
        Temporary
    };

    explicit TopProxy(Type type, QObject* parent = nullptr);
    ~TopProxy() override;

    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
    bool filterAcceptsColumn(int source_column, const QModelIndex& source_parent) const override;

private:
    Type m_type;
};

#endif // TOPPROXY_H
