/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "treeproxy.h"
#include "locationdata.h"

#include <resultdata.h>
#include <QDebug>

TreeProxy::TreeProxy(int symbolRole, int resultDataRole, QObject* parent)
    : QSortFilterProxyModel(parent)
    , m_symbolRole(symbolRole)
    , m_resultDataRole(resultDataRole)
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

    const auto index = source->index(sourceRow, 0, sourceParent);
    const auto* resultData = index.data(m_resultDataRole).value<const ResultData*>();
    Q_ASSERT(resultData);

    auto filterOut = [&](StringIndex stringId, const QString& filter) {
        return !filter.isEmpty() && !resultData->string(stringId).contains(filter, Qt::CaseInsensitive);
    };

    const auto symbol = index.data(m_symbolRole).value<Symbol>();
    if (filterOut(symbol.functionId, m_functionFilter) || filterOut(symbol.moduleId, m_moduleFilter)) {
        return false;
    }
    return true;
}

bool TreeProxy::lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const
{
    if (sortColumn() != 0) {
        return QSortFilterProxyModel::lessThan(source_left, source_right);
    }

    const auto* resultData = source_left.data(m_resultDataRole).value<const ResultData*>();

    const auto symbol_left = source_left.data(m_symbolRole).value<Symbol>();
    const auto symbol_right = source_right.data(m_symbolRole).value<Symbol>();

    if (symbol_left.functionId != symbol_right.functionId) {
        return resultData->string(symbol_left.functionId) < resultData->string(symbol_right.functionId);
    }

    const auto path_left = resultData->string(symbol_left.moduleId);
    const auto path_right = resultData->string(symbol_right.moduleId);

    auto toShortPath = [](const QString& path) {
        int idx = path.lastIndexOf(QLatin1Char('/'));
        return path.midRef(idx + 1);
    };

    return toShortPath(path_left) < toShortPath(path_right);
}
