/*
 * Copyright 2016-2019 Milian Wolff <mail@milianw.de>
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

#ifndef COSTDELEGATE_H
#define COSTDELEGATE_H

#include <QStyledItemDelegate>

class CostDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit CostDelegate(int sortRole, int maxCostRole, QObject* parent = nullptr);
    ~CostDelegate();

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    int m_sortRole = -1;
    int m_maxCostRole = -1;
};

#endif // COSTDELEGATE_H
