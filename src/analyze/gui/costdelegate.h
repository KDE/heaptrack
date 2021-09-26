/*
    SPDX-FileCopyrightText: 2016-2019 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
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
