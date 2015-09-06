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

#include "flamegraph.h"

#include <cmath>

#include <QVBoxLayout>
#include <QGraphicsScene>
#include <QStyleOption>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QGraphicsSimpleTextItem>
#include <QWheelEvent>
#include <QEvent>

#include <QElapsedTimer>
#include <QDebug>

#include <KLocalizedString>
#include <KColorScheme>

FrameGraphicsItem::FrameGraphicsItem(const quint64 cost, const QString& function, FrameGraphicsItem* parent)
    : QGraphicsRectItem(parent)
    , m_cost(cost)
    , m_isHovered(false)
{
    static const QString emptyLabel = QStringLiteral("???");

    m_label = i18nc("%1: number of allocations, %2: function label",
                    "%2: %1",
                    cost,
                    function.isEmpty() ? emptyLabel : function);
    setToolTip(m_label);
    setFlag(QGraphicsItem::ItemIsSelectable);
    setAcceptHoverEvents(true);
}

quint64 FrameGraphicsItem::cost() const
{
    return m_cost;
}

void FrameGraphicsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* /*widget*/)
{
    if (isSelected() || m_isHovered) {
        auto selectedColor = brush().color();
        selectedColor.setAlpha(255);
        painter->fillRect(rect(), selectedColor);
    } else {
        painter->fillRect(rect(), brush());
    }

    const QPen oldPen = painter->pen();
    auto pen = oldPen;
    pen.setColor(brush().color());
    if (isSelected()) {
        pen.setWidth(2);
    }
    painter->setPen(pen);
    painter->drawRect(rect());
    painter->setPen(oldPen);

    const int margin = 4;
    const int width = rect().width() - 2 * margin;
    if (width < option->fontMetrics.averageCharWidth() * 6) {
        // text is too wide for the current LOD, don't paint it
        return;
    }

    const int height = rect().height();

    painter->drawText(margin + rect().x(), rect().y(), width, height, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                      option->fontMetrics.elidedText(m_label, Qt::ElideRight, width));
}

void FrameGraphicsItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    QGraphicsRectItem::hoverEnterEvent(event);
    m_isHovered = true;
}

void FrameGraphicsItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    QGraphicsRectItem::hoverLeaveEvent(event);
    m_isHovered = false;
}

namespace {

struct Frame {
    quint64 cost = 0;
    using Stack = QMap<QString, Frame>;
    Stack children;
};
using Stack = Frame::Stack;

/**
 * Generate a color from the "mem" color space used in upstream FlameGraph.pl
 */
QColor color()
{
    return QColor(0, 190 + 50 * qreal(rand()) / RAND_MAX, 210 * qreal(rand()) / RAND_MAX, 125);
}

/**
 * Layout the flame graph and hide tiny items.
 */
void layoutItems(FrameGraphicsItem *parent)
{
    const auto& parentRect = parent->rect();
    const auto pos = parentRect.topLeft();
    const qreal maxWidth = parentRect.width();
    const qreal h = parentRect.height();
    const qreal y_margin = 2.;
    const qreal y = pos.y() - h - y_margin;
    qreal x = pos.x();

    foreach (auto child, parent->childItems()) {
        auto frameChild = static_cast<FrameGraphicsItem*>(child);
        const qreal w = maxWidth * double(frameChild->cost()) / parent->cost();
        frameChild->setVisible(w > 1);
        frameChild->setRect(QRectF(x, y, w, h));
        layoutItems(frameChild);
        x += w;
    }
}

/**
 * Convert the top-down graph into a tree of FrameGraphicsItem.
 */
void toGraphicsItems(const Stack& data, FrameGraphicsItem *parent)
{
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        FrameGraphicsItem* item = new FrameGraphicsItem(it.value().cost, it.key(), parent);
        item->setPen(parent->pen());
        item->setBrush(color());
        toGraphicsItems(it.value().children, item);
    }
}

/**
 * Build a top-down graph from the bottom-up data in @p mergedAllocations
 */
void buildFlameGraph(const QVector<RowData>& mergedAllocations, Stack* topStack)
{
    foreach (const auto& row, mergedAllocations) {
        if (row.children.isEmpty()) {
            // leaf node found, bubble up the parent chain to build a top-down tree
            auto node = &row;
            auto stack = topStack;
            while (node) {
                auto& data = (*stack)[node->location.function];
                // always use the leaf node's cost and propagate that one up the chain
                // otherwise we'd count the cost of some nodes multiple times
                data.cost += row.allocations;
                stack = &data.children;
                node = node->parent;
            }
        } else {
            // recurse to find a leaf
            buildFlameGraph(row.children, topStack);
        }
    }
}

}

FlameGraph::FlameGraph(QWidget* parent, Qt::WindowFlags flags)
    : QWidget(parent, flags)
    , m_scene(new QGraphicsScene(this))
    , m_view(new QGraphicsView(this))
    , m_rootItem(nullptr)
    , m_minRootWidth(0)
{
    setLayout(new QVBoxLayout);

    m_scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    m_view->setScene(m_scene);
    m_view->viewport()->installEventFilter(this);
    m_view->viewport()->setMouseTracking(true);
    m_view->setFont(QFont(QStringLiteral("monospace")));

    layout()->addWidget(m_view);
}

FlameGraph::~FlameGraph() = default;

bool FlameGraph::eventFilter(QObject* object, QEvent* event)
{
    bool ret = QObject::eventFilter(object, event);

    if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            zoomInto(static_cast<FrameGraphicsItem*>(m_view->itemAt(mouseEvent->pos())));
        }
    } else if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
        zoomInto(m_rootItem);
    }
    return ret;
}

void FlameGraph::setData(FrameGraphicsItem* rootItem)
{
    m_scene->clear();
    m_rootItem = rootItem;
    // layouting needs a root item with a given height, the rest will be overwritten later
    rootItem->setRect(0, 0, 800, m_view->fontMetrics().height() + 4);
    m_scene->addItem(rootItem);

    zoomInto(m_rootItem);
}

FrameGraphicsItem* FlameGraph::parseData(const QVector<RowData>& data)
{
    Stack stack;
    buildFlameGraph(data, &stack);

    double totalCost = 0;
    foreach(const auto& frame, stack) {
        totalCost += frame.cost;
    }

    KColorScheme scheme(QPalette::Active);
    const QPen pen(scheme.foreground().color());

    auto rootItem = new FrameGraphicsItem(totalCost, i18n("total allocations"));
    rootItem->setBrush(scheme.background());
    rootItem->setPen(pen);
    toGraphicsItems(stack, rootItem);
    return rootItem;
}

void FlameGraph::zoomInto(FrameGraphicsItem* item)
{
    if (!item) {
        return;
    }

    // scale item and its parents to the maximum available width
    // also hide all siblings of the parent items
    const auto rootWidth = m_view->viewport()->width() - 40;
    auto parent = item;
    while (parent) {
        auto rect = parent->rect();
        rect.setLeft(0);
        rect.setWidth(rootWidth);
        parent->setRect(rect);
        if (parent->parentItem()) {
            foreach (auto sibling, parent->parentItem()->childItems()) {
                sibling->setVisible(sibling == parent);
            }
        }
        parent = static_cast<FrameGraphicsItem*>(parent->parentItem());
    }

    // then layout all items below the selected on
    layoutItems(item);

    // and make sure it's visible
    m_view->centerOn(item);
}
