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
#include <QGraphicsRectItem>
#include <QWheelEvent>
#include <QEvent>
#include <QToolTip>
#include <QDebug>

#include <KLocalizedString>
#include <KColorScheme>

class FrameGraphicsItem : public QGraphicsRectItem
{
public:
    FrameGraphicsItem(const quint64 cost, const QString& function, FrameGraphicsItem* parent = nullptr);

    quint64 cost() const;
    void setCost(quint64 cost);
    QString function() const;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;
    void showToolTip() const;

private:
    quint64 m_cost;
    QString m_function;
    bool m_isHovered;
};

FrameGraphicsItem::FrameGraphicsItem(const quint64 cost, const QString& function, FrameGraphicsItem* parent)
    : QGraphicsRectItem(parent)
    , m_cost(cost)
    , m_function(function)
    , m_isHovered(false)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    setAcceptHoverEvents(true);
}

quint64 FrameGraphicsItem::cost() const
{
    return m_cost;
}

void FrameGraphicsItem::setCost(quint64 cost)
{
    m_cost = cost;
}

QString FrameGraphicsItem::function() const
{
    return m_function;
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
                      option->fontMetrics.elidedText(m_function, Qt::ElideRight, width));
}

void FrameGraphicsItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    QGraphicsRectItem::hoverEnterEvent(event);
    m_isHovered = true;
    showToolTip();
}

void FrameGraphicsItem::showToolTip() const
{
    // we build the tooltip text on demand, which is much faster than doing that for potentially thousands of items when we load the data
    QToolTip::showText(QCursor::pos(), i18nc("%1: number of allocations, %2: function label", "%1 allocations in %2 and below.", m_cost, m_function));
}

void FrameGraphicsItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    QGraphicsRectItem::hoverLeaveEvent(event);
    m_isHovered = false;

    if (auto parent = static_cast<FrameGraphicsItem*>(parentItem())) {
        parent->showToolTip();
    }
}

namespace {

/**
 * Generate a brush from the "mem" color space used in upstream FlameGraph.pl
 */
QBrush brush()
{
    // intern the brushes, to reuse them across items which can be thousands
    // otherwise we'd end up with dozens of allocations and higher memory consumption
    static QVector<QBrush> brushes;
    if (brushes.isEmpty()) {
        std::generate_n(std::back_inserter(brushes), 100, [] () {
            return QColor(0, 190 + 50 * qreal(rand()) / RAND_MAX, 210 * qreal(rand()) / RAND_MAX, 125);
        });
    }
    return brushes.at(rand() % brushes.size());
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

FrameGraphicsItem* findItemByFunction(const QList<QGraphicsItem*>& items, const QString& function)
{
    foreach (auto item_, items) {
        auto item = static_cast<FrameGraphicsItem*>(item_);
        if (item->function() == function) {
            return item;
        }
    }
    return nullptr;
}

/**
 * Convert the top-down graph into a tree of FrameGraphicsItem.
 */
void toGraphicsItems(const QVector<RowData>& data, FrameGraphicsItem *parent)
{
    foreach (const auto& row, data) {
        auto item = findItemByFunction(parent->childItems(), row.location.function);
        if (!item) {
            item = new FrameGraphicsItem(row.allocations, row.location.function, parent);
            item->setPen(parent->pen());
            item->setBrush(brush());
        } else {
            item->setCost(item->cost() + row.allocations);
        }
        toGraphicsItems(row.children, item);
    }
}

FrameGraphicsItem* parseData(const QVector<RowData>& topDownData)
{
    double totalCost = 0;
    foreach(const auto& frame, topDownData) {
        totalCost += frame.allocations;
    }

    KColorScheme scheme(QPalette::Active);
    const QPen pen(scheme.foreground().color());

    auto rootItem = new FrameGraphicsItem(totalCost, i18n("%1 allocations in total", totalCost));
    rootItem->setBrush(scheme.background());
    rootItem->setPen(pen);
    toGraphicsItems(topDownData, rootItem);
    return rootItem;
}

}

FlameGraph::FlameGraph(QWidget* parent, Qt::WindowFlags flags)
    : QWidget(parent, flags)
    , m_scene(new QGraphicsScene(this))
    , m_view(new QGraphicsView(this))
    , m_rootItem(nullptr)
    , m_selectedItem(nullptr)
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
            selectItem(static_cast<FrameGraphicsItem*>(m_view->itemAt(mouseEvent->pos())));
        }
    } else if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
        selectItem(m_selectedItem);
    }
    return ret;
}

void FlameGraph::showEvent(QShowEvent* event)
{
    setData(parseData(m_topDownData));
    QWidget::showEvent(event);
}

void FlameGraph::hideEvent(QHideEvent* event)
{
    setData(nullptr);
    QWidget::hideEvent(event);
}

void FlameGraph::setTopDownData(const TreeData& topDownData)
{
    m_topDownData = topDownData;
    if (isVisible()) {
        setData(parseData(topDownData));
    }
}

void FlameGraph::setData(FrameGraphicsItem* rootItem)
{
    m_scene->clear();
    m_rootItem = rootItem;
    m_selectedItem = rootItem;
    if (!rootItem) {
        return;
    }

    // layouting needs a root item with a given height, the rest will be overwritten later
    rootItem->setRect(0, 0, 800, m_view->fontMetrics().height() + 4);
    m_scene->addItem(rootItem);

    if (isVisible()) {
        selectItem(m_rootItem);
    }
}

void FlameGraph::selectItem(FrameGraphicsItem* item)
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

    m_selectedItem = item;
}
