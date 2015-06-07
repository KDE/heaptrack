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

void Proxy::setFunctionFilter(const QString& functionFilter)
{
    m_functionFilter = functionFilter;
    invalidate();
}

void Proxy::setFileFilter(const QString& fileFilter)
{
    m_fileFilter = fileFilter;
    invalidate();
}

void Proxy::setModuleFilter(const QString& moduleFilter)
{
    m_moduleFilter = moduleFilter;
    invalidate();
}

bool Proxy::acceptRow(int sourceRow, const QModelIndex& sourceParent) const
{
    auto source = sourceModel();
    if (!source) {
        return false;
    }
    if (!m_functionFilter.isEmpty()) {
        const auto& function = source->index(sourceRow, Model::FunctionColumn, sourceParent).data().toString();
        if (!function.contains(m_functionFilter)) {
            return false;
        }
    }
    if (!m_fileFilter.isEmpty()) {
        const auto& file = source->index(sourceRow, Model::FileColumn, sourceParent).data().toString();
        if (!file.contains(m_fileFilter)) {
            return false;
        }
    }
    if (!m_moduleFilter.isEmpty()) {
        const auto& module = source->index(sourceRow, Model::ModuleColumn, sourceParent).data().toString();
        if (!module.contains(m_moduleFilter)) {
            return false;
        }
    }
    return true;
}
