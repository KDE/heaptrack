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

#include "stacksmodel.h"
#include "treemodel.h"

#include <KLocalizedString>

#include <QDebug>

StacksModel::StacksModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

StacksModel::~StacksModel() = default;

void StacksModel::setStackIndex(int index)
{
    beginResetModel();
    m_stackIndex = index - 1;
    endResetModel();
}

static void findLeafs(const QModelIndex& index, QVector<QModelIndex>* leafs)
{
    auto model = index.model();
    Q_ASSERT(model);
    int rows = model->rowCount(index);
    if (!rows) {
        leafs->append(index);
        return;
    }
    for (int i = 0; i < rows; ++i) {
        findLeafs(model->index(i, 0, index), leafs);
    }
}

void StacksModel::fillFromIndex(const QModelIndex& index)
{
    if (index.column() != 0) {
        // only the first column has children
        fillFromIndex(index.sibling(index.row(), 0));
        return;
    }

    QVector<QModelIndex> leafs;
    findLeafs(index, &leafs);

    beginResetModel();
    m_data.clear();
    m_data.resize(leafs.size());
    m_stackIndex = 0;
    int stackIndex = 0;
    foreach (QModelIndex leaf, leafs) {
        auto& stack = m_data[stackIndex];
        while (leaf.isValid()) {
            stack << leaf.sibling(leaf.row(), TreeModel::LocationColumn);
            leaf = leaf.parent();
        }
        std::reverse(stack.begin(), stack.end());
        ++stackIndex;
    }
    endResetModel();

    emit stacksFound(m_data.size());
}

void StacksModel::clear()
{
    beginResetModel();
    m_data.clear();
    endResetModel();
    emit stacksFound(0);
}

int StacksModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || m_data.isEmpty()) {
        return 0;
    }
    return m_data.value(m_stackIndex).size();
}

QVariant StacksModel::data(const QModelIndex& index, int role) const
{
    if (!hasIndex(index.row(), index.column(), index.parent())) {
        return {};
    }
    return m_data.value(m_stackIndex).value(index.row()).data(role);
}

QVariant StacksModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (section == 0 && role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        return i18n("Backtrace");
    }
    return {};
}
