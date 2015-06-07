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

#ifndef PROXY_H
#define PROXY_H

#include <KRecursiveFilterProxyModel>

class Proxy final : public KRecursiveFilterProxyModel
{
    Q_OBJECT
public:
    explicit Proxy(QObject* parent);
    virtual ~Proxy();

protected:
    bool acceptRow(int source_row, const QModelIndex& source_parent) const override;
};

#endif // PROXY_H
