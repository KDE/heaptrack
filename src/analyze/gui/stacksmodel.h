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

#ifndef STACKSMODEL_H
#define STACKSMODEL_H

#include <QAbstractListModel>

class StacksModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit StacksModel(QObject* parent = nullptr);
    ~StacksModel();

    void setStackIndex(int index);
    void fillFromIndex(const QModelIndex& leaf);
    void clear();

    int rowCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

signals:
    void stacksFound(int stacks);

private:
    QVector<QVector<QModelIndex>> m_data;
    int m_stackIndex = 0;
};

#endif // STACKSMODEL_H
