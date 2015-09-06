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

#include "treeproxy.h"

#include "treemodel.h"

TreeProxy::TreeProxy(QObject* parent)
    : KRecursiveFilterProxyModel(parent)
{
}

TreeProxy::~TreeProxy() = default;

void TreeProxy::setFunctionFilter(const QString& functionFilter)
{
    m_functionFilter = functionFilter;
    invalidate();
}

void TreeProxy::setFileFilter(const QString& fileFilter)
{
    m_fileFilter = fileFilter;
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
        const auto& function = source->index(sourceRow, TreeModel::FunctionColumn, sourceParent).data().toString();
        if (!function.contains(m_functionFilter, Qt::CaseInsensitive)) {
            return false;
        }
    }
    if (!m_fileFilter.isEmpty()) {
        const auto& file = source->index(sourceRow, TreeModel::FileColumn, sourceParent).data().toString();
        if (!file.contains(m_fileFilter, Qt::CaseInsensitive)) {
            return false;
        }
    }
    if (!m_moduleFilter.isEmpty()) {
        const auto& module = source->index(sourceRow, TreeModel::ModuleColumn, sourceParent).data().toString();
        if (!module.contains(m_moduleFilter, Qt::CaseInsensitive)) {
            return false;
        }
    }
    return true;
}
