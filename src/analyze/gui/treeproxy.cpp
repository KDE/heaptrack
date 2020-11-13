/*
 * Copyright 2015-2017 Milian Wolff <mail@milianw.de>
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

#include "treeproxy.h"
#include "locationdata.h"

#include <QDebug>

TreeProxy::TreeProxy(int symbolRole, QObject* parent)
    : QSortFilterProxyModel(parent)
    , m_symbolRole(symbolRole)
{
    setRecursiveFilteringEnabled(true);
    setSortLocaleAware(false);
}

TreeProxy::~TreeProxy() = default;

void TreeProxy::setFunctionFilter(const QString& functionFilter)
{
    m_functionFilter = functionFilter;
    invalidate();
}

void TreeProxy::setModuleFilter(const QString& moduleFilter)
{
    m_moduleFilter = moduleFilter;
    invalidate();
}

bool TreeProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    auto source = sourceModel();
    if (!source) {
        return false;
    }

    if (m_functionFilter.isEmpty() && m_moduleFilter.isEmpty()) {
        return true;
    }

    const auto& symbol = source->index(sourceRow, 0, sourceParent).data(m_symbolRole).value<Symbol>();
    if (!m_functionFilter.isEmpty() && !symbol.symbol.contains(m_functionFilter, Qt::CaseInsensitive)) {
        return false;
    }
    if (!m_moduleFilter.isEmpty() && !symbol.binary.contains(m_moduleFilter, Qt::CaseInsensitive)) {
        return false;
    }
    return true;
}
