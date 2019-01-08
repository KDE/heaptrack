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

#include <QDebug>

TreeProxy::TreeProxy(int functionColumn, int moduleColumn, QObject* parent)
    : KRecursiveFilterProxyModel(parent)
    , m_functionColumn(functionColumn)
    , m_moduleColumn(moduleColumn)
{
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

bool TreeProxy::acceptRow(int sourceRow, const QModelIndex& sourceParent) const
{
    auto source = sourceModel();
    if (!source) {
        return false;
    }
    if (!m_functionFilter.isEmpty()) {
        const auto& function = source->index(sourceRow, m_functionColumn, sourceParent).data().toString();
        if (!function.contains(m_functionFilter, Qt::CaseInsensitive)) {
            return false;
        }
    }
    if (!m_moduleFilter.isEmpty()) {
        const auto& module = source->index(sourceRow, m_moduleColumn, sourceParent).data().toString();
        if (!module.contains(m_moduleFilter, Qt::CaseInsensitive)) {
            return false;
        }
    }
    return true;
}
