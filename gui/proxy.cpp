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

#include "proxy.h"

#include "model.h"

Proxy::Proxy(QObject* parent)
    : KRecursiveFilterProxyModel(parent)
{
}

Proxy::~Proxy() = default;

bool Proxy::acceptRow(int source_row, const QModelIndex& source_parent) const
{
    auto source = sourceModel();
    if (!source) {
        return false;
    }
    const auto& needle = filterRegExp().pattern();
    if (needle.isEmpty()) {
        return true;
    }
    for (auto column : {Model::FunctionColumn, Model::FileColumn, Model::ModuleColumn}) {
        const auto& haystack = source->index(source_row, column, source_parent).data(Qt::DisplayRole).toString();
        if (haystack.contains(needle)) {
            return true;
        }
    }
    return false;
}
