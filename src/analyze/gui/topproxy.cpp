/*
    SPDX-FileCopyrightText: 2016-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "topproxy.h"

#include <KLocalizedString>

namespace {
TreeModel::Columns toSource(TopProxy::Type type)
{
    switch (type) {
    case TopProxy::Peak:
        return TreeModel::PeakColumn;
    case TopProxy::Leaked:
        return TreeModel::LeakedColumn;
    case TopProxy::Allocations:
        return TreeModel::AllocationsColumn;
    case TopProxy::Temporary:
        return TreeModel::TemporaryColumn;
    }
    Q_UNREACHABLE();
}
}

TopProxy::TopProxy(Type type, QObject* parent)
    : QSortFilterProxyModel(parent)
    , m_type(type)
{
}

TopProxy::~TopProxy() = default;

bool TopProxy::filterAcceptsColumn(int source_column, const QModelIndex& /*source_parent*/) const
{
    return source_column == TreeModel::LocationColumn || source_column == toSource(m_type);
}

bool TopProxy::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const
{
    if (source_parent.isValid()) {
        // only show top rows
        return false;
    }
    const auto index = sourceModel()->index(source_row, toSource(m_type));
    const auto cost = index.data(TreeModel::SortRole).toLongLong();
    // note: explicitly exclude zero values, which could show up when we diff files
    //       and no change was observed (overall) for a given metric
    if (!cost || cost < m_costThreshold) {
        // don't show rows that didn't leak anything, or didn't trigger any
        // temporary allocations
        // in general, hide anything that's not really interesting
        return false;
    }
    return true;
}

void TopProxy::setSourceModel(QAbstractItemModel* sourceModel)
{
    QSortFilterProxyModel::setSourceModel(sourceModel);
    connect(sourceModel, &QAbstractItemModel::modelReset, this, &TopProxy::updateCostThreshold, Qt::UniqueConnection);
    updateCostThreshold();
}

void TopProxy::updateCostThreshold()
{
    // hide anything below 1% of the max cost
    m_costThreshold = sourceModel()->index(0, toSource(m_type)).data(TreeModel::MaxCostRole).toLongLong() * 0.01;
    invalidate();
}
