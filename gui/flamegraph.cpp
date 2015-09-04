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

FrameGraphicsItem::FrameGraphicsItem(const QRectF& rect, const quint64 cost, const QString& function, FrameGraphicsItem* parent)
    : QGraphicsRectItem(rect, parent)
{
    static const QString emptyLabel = QStringLiteral("???");

    m_label = i18nc("%1: number of allocations, %2: function label",
                    "%2: %1",
                    cost,
                    function.isEmpty() ? emptyLabel : function);
    setToolTip(m_label);
    setFlag(QGraphicsItem::ItemIsSelectable);
}

QFont FrameGraphicsItem::font()
{
    static const QFont font(QStringLiteral("monospace"), 10);
    return font;
}

QFontMetrics FrameGraphicsItem::fontMetrics()
{
    static const QFontMetrics metrics(font());
    return metrics;
}

int FrameGraphicsItem::margin()
{
    return 5;
}

int FrameGraphicsItem::itemHeight()
{
    return fontMetrics().height() + 4;
}

int FrameGraphicsItem::preferredWidth() const
{
    return fontMetrics().width(m_label) + 2 * margin();
}

void FrameGraphicsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    const int width = rect().width() - 2 * margin();
    if (width < 2) {
        // don't try to paint tiny items
        return;
    }

    if (isSelected()) {
        auto selectedColor = brush().color();
        selectedColor.setAlpha(255);
        painter->fillRect(rect(), selectedColor);
        const QPen oldPen = painter->pen();
        auto pen = oldPen;
        pen.setWidth(2);
        painter->setPen(pen);
        painter->drawRect(rect());
        painter->setPen(oldPen);
    } else {
        painter->fillRect(rect(), brush());
        painter->drawRect(rect());
    }

    // TODO: text should always be displayed in a constant size and not zoomed
    // TODO: items should only be scaled horizontally, not vertically
    // TODO: items should "fit" into the view width
    if (width < fontMetrics().averageCharWidth() * 6) {
        // text is too wide for the current LOD, don't paint it
        return;
    }

    const int height = rect().height();

    const QFont oldFont = painter->font();
    painter->setFont(font());
    painter->drawText(margin() + rect().x(), rect().y(), width, height, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                        fontMetrics().elidedText(m_label, Qt::ElideRight, width));
    painter->setFont(oldFont);
}

namespace {

void scaleItems(FrameGraphicsItem *item, qreal scaleFactor)
{
    auto rect = item->rect();
    rect.moveLeft(rect.left() * scaleFactor);
    rect.setWidth(rect.width() * scaleFactor);
    item->setRect(rect);
    item->setVisible(rect.width() > 2.);
    foreach (auto child, item->childItems()) {
        if (auto frameChild = dynamic_cast<FrameGraphicsItem*>(child)) {
            scaleItems(frameChild, scaleFactor);
        }
    }
}

struct Frame {
    quint64 cost = 0;
    using Stack = QMap<QString, Frame>;
    Stack children;
};
using Stack = Frame::Stack;

QColor color(quint64 cost, quint64 maxCost)
{
    const double ratio = double(cost) / maxCost;
    return QColor::fromHsv(120 - ratio * 120, 255, 255, (-((ratio-1) * (ratio-1))) * 120 + 120);
}


void toGraphicsItems(const Stack& data, qreal totalCostForColor,
                     qreal parentCost, FrameGraphicsItem *parent)
{
    auto pos = parent->rect().topLeft();
    const qreal h = FrameGraphicsItem::itemHeight();
    const qreal y_margin = 2.;
    const qreal y = pos.y() - h - y_margin;
    const qreal maxWidth = parent->rect().width();
    qreal x = pos.x();

    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        const qreal w = maxWidth * double(it.value().cost) / parentCost;
        FrameGraphicsItem* item = new FrameGraphicsItem(QRectF(x, y, w, h), it.value().cost, it.key(), parent);
        item->setVisible(w > 2.);
        item->setPen(parent->pen());
        item->setBrush(color(it.value().cost, totalCostForColor));
        toGraphicsItems(it.value().children, totalCostForColor, it.value().cost, item);
        x += w;
    }
}

FrameGraphicsItem* buildGraphicsItems(const Stack& stack)
{
    double totalCost = 0;
    foreach(const auto& frame, stack) {
        totalCost += frame.cost;
    }

    KColorScheme scheme(QPalette::Active);
    const QPen pen(scheme.foreground().color());

    auto rootItem = new FrameGraphicsItem(QRectF(0, 0, 1000, FrameGraphicsItem::itemHeight()), totalCost, i18n("total allocations"));
    rootItem->setBrush(scheme.background());
    rootItem->setPen(pen);
    toGraphicsItems(stack, totalCost, totalCost, rootItem);
    return rootItem;
}

static void buildFlameGraph(const QVector<RowData>& mergedAllocations, Stack* topStack)
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
    // prevent duplicate resize, when a scrollbar is shown for the first time
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    layout()->addWidget(m_view);
}

FlameGraph::~FlameGraph()
{

}

bool FlameGraph::eventFilter(QObject* object, QEvent* event)
{
    bool ret = QObject::eventFilter(object, event);

    if (event->type() == QEvent::Wheel) {
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
        if (wheelEvent->modifiers() == Qt::ControlModifier) {
            // zoom view with Ctrl + mouse wheel
            qreal scale = pow(1.1, double(wheelEvent->delta()) / (120.0 * 2.));
            m_view->scale(scale, scale);
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            auto item = dynamic_cast<FrameGraphicsItem*>(m_view->itemAt(mouseEvent->pos()));
            if (item) {
                zoomInto(item);
            }
        }
    } else if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
        zoomIntoRootItem();
    }
    return ret;
}

void FlameGraph::setData(FrameGraphicsItem* rootItem)
{
    m_scene->clear();
    m_rootItem = rootItem;
    m_scene->addItem(rootItem);

    m_minRootWidth = 0;
    zoomIntoRootItem();
}

FrameGraphicsItem * FlameGraph::parseData(const QVector<RowData>& data)
{
    Stack stack;
    buildFlameGraph(data, &stack);
    return buildGraphicsItems(stack);
}

void FlameGraph::zoomInto(FrameGraphicsItem* item)
{
    const auto preferredWidth = item->preferredWidth();
    auto scaleFactor = qreal(preferredWidth) / item->rect().width();
    if (m_rootItem->rect().width() * scaleFactor < m_minRootWidth) {
        // don't shrink too much, keep the root item at a certain minimum size
        scaleFactor = m_minRootWidth / m_rootItem->rect().width();
    }
    scaleItems(m_rootItem, scaleFactor);
    m_view->centerOn(item);
}

void FlameGraph::zoomIntoRootItem()
{
    const auto minRootWidth = m_view->viewport()->width() - 20;
    if (!isVisible() || !m_rootItem || m_minRootWidth == minRootWidth)
        return;

    m_minRootWidth = minRootWidth;
    zoomInto(m_rootItem);
}
