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

class FrameGraphicsItem : public QGraphicsRectItem
{
public:
    FrameGraphicsItem(const QRectF& rect, const quint64 cost, const QString& function, FrameGraphicsItem* parent = nullptr)
        : QGraphicsRectItem(rect, parent)
    {
        static const QString emptyLabel = QStringLiteral("???");

        m_label = i18nc("%1: number of allocations, %2: function label",
                        "%2: %1",
                        cost,
                        function.isEmpty() ? emptyLabel : function);
        setToolTip(m_label);
    }

    static const QFont font()
    {
        static const QFont font(QStringLiteral("monospace"), 10);
        return font;
    }

    static const QFontMetrics fontMetrics()
    {
        static const QFontMetrics metrics(font());
        return metrics;
    }

    static int margin()
    {
        return 5;
    }

    int preferredWidth() const
    {
        return fontMetrics().width(m_label) + 2 * margin();
    }

    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = 0)
    {
        const int width = rect().width() - 2 * margin();
        if (width < 2) {
            // don't try to paint tiny items
            return;
        }

        QGraphicsRectItem::paint(painter, option, widget);

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

private:
    QString m_label;
};

namespace {

QColor color(quint64 cost, quint64 maxCost)
{
    const double ratio = double(cost) / maxCost;
    return QColor::fromHsv(120 - ratio * 120, 255, 255, (-((ratio-1) * (ratio-1))) * 120 + 120);
}

const qreal h = 25.;
const qreal x_margin = 0.;
const qreal y_margin = 2.;
const qreal minRootWidth = 800.;

// TODO: what is the right value for maxWidth here?
void toGraphicsItems(const FlameGraphData::Stack& data, qreal totalCostForColor,
                     qreal parentCost, FrameGraphicsItem *parent)
{
    auto pos = parent->rect().topLeft();
    qreal x = pos.x();
    const qreal y = pos.y() - h - y_margin;
    const qreal maxWidth = parent->rect().width();

    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        const qreal w = maxWidth * double(it.value().cost) / parentCost;
        FrameGraphicsItem* item = new FrameGraphicsItem(QRectF(x, y, w, h), it.value().cost, it.key(), parent);
        item->setVisible(w > 2.);
        item->setPen(parent->pen());
        item->setBrush(color(it.value().cost, totalCostForColor));
        toGraphicsItems(it.value().children, totalCostForColor, it.value().cost, item);
        x += w + x_margin;
    }
}

void scaleItems(FrameGraphicsItem *item, qreal scaleFactor)
{
    auto rect = item->rect();
    rect.moveLeft(rect.left() * scaleFactor);
    rect.setWidth(rect.width() * scaleFactor);
    item->setRect(rect);
    foreach (auto child, item->childItems()) {
        if (auto frameChild = dynamic_cast<FrameGraphicsItem*>(child)) {
            scaleItems(frameChild, scaleFactor);
        }
    }
}

}

FlameGraph::FlameGraph(QWidget* parent, Qt::WindowFlags flags)
    : QWidget(parent, flags)
    , m_scene(new QGraphicsScene(this))
    , m_view(new QGraphicsView(this))
    , m_rootItem(nullptr)
{
    qRegisterMetaType<FlameGraphData>();

    setLayout(new QVBoxLayout);

    m_view->setScene(m_scene);
    m_view->viewport()->installEventFilter(this);

    layout()->addWidget(m_view);
}

FlameGraph::~FlameGraph()
{

}

bool FlameGraph::eventFilter(QObject* object, QEvent* event)
{
    if (object != m_view->viewport()) {
        return QObject::eventFilter(object, event);
    }

    if (event->type() == QEvent::Wheel) {
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
        if (wheelEvent->modifiers() == Qt::ControlModifier) {
            // zoom view with Ctrl + mouse wheel
            qreal scale = pow(1.1, double(wheelEvent->delta()) / (120.0 * 2.));
            m_view->scale(scale, scale);
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            auto item = dynamic_cast<FrameGraphicsItem*>(m_view->itemAt(mouseEvent->pos()));
            if (item) {
                const auto preferredWidth = item->preferredWidth();
                auto scaleFactor = qreal(preferredWidth) / item->rect().width();
                if (m_rootItem->rect().width() * scaleFactor < minRootWidth) {
                    // don't shrink too much, keep the root item at a certain minimum size
                    scaleFactor = minRootWidth / m_rootItem->rect().width();
                }
                scaleItems(m_rootItem, scaleFactor);
                m_view->centerOn(item);
            }
            return true;
        }
    }
    return QObject::eventFilter(object, event);
}

void FlameGraph::setData(const FlameGraphData& data)
{
    m_data = data;
    m_scene->clear();

    qDebug() << "Evaluating flame graph";
    QElapsedTimer t; t.start();

    double totalCost = 0;
    foreach(const auto& frame, data.stack) {
        totalCost += frame.cost;
    }

    const QPen pen(KColorScheme(QPalette::Active).foreground().color());

    m_rootItem = new FrameGraphicsItem(QRectF(0, 0, minRootWidth, h), totalCost, i18n("total allocations"));
    m_rootItem->setPen(pen);
    toGraphicsItems(data.stack, totalCost, totalCost, m_rootItem);
    m_scene->addItem(m_rootItem);

    qDebug() << "took me: " << t.elapsed();
}
