/*
    SPDX-FileCopyrightText: 2016-2019 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "costdelegate.h"

#include <QDebug>
#include <QPainter>

#include <cmath>

CostDelegate::CostDelegate(int sortRole, int maxCostRole, QObject* parent)
    : QStyledItemDelegate(parent)
    , m_sortRole(sortRole)
    , m_maxCostRole(maxCostRole)
{
}

CostDelegate::~CostDelegate() = default;

void CostDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    // TODO: handle negative values
    const int64_t cost = index.data(m_sortRole).toULongLong();
    if (cost == 0) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    const int64_t maxCost = index.data(m_maxCostRole).toULongLong();
    // top-down can miscalculate the peak cost
    const auto fraction = std::min(1.f, std::abs(float(cost) / maxCost));
    auto rect = option.rect;
    rect.setWidth(rect.width() * fraction);

    const auto& brush = painter->brush();
    const auto& pen = painter->pen();

    painter->setPen(Qt::NoPen);

    if (option.features & QStyleOptionViewItem::Alternate) {
        // we must handle this ourselves as otherwise the custom background
        // would get painted over with the alternate background color
        painter->setBrush(option.palette.alternateBase());
        painter->drawRect(option.rect);
    }

    auto color = QColor::fromHsv(120 - fraction * 120, 255, 255, (-((fraction - 1) * (fraction - 1))) * 120 + 120);
    painter->setBrush(color);
    painter->drawRect(rect);

    painter->setBrush(brush);
    painter->setPen(pen);

    if (option.features & QStyleOptionViewItem::Alternate) {
        auto o = option;
        o.features &= ~QStyleOptionViewItem::Alternate;
        QStyledItemDelegate::paint(painter, o, index);
    } else {
        QStyledItemDelegate::paint(painter, option, index);
    }
}

#include "moc_costdelegate.cpp"
