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

namespace {

QColor color(quint64 cost, quint64 maxCost)
{
    const double ratio = double(cost) / maxCost;
    return QColor::fromHsv(120 - ratio * 120, 255, 255, (-((ratio-1) * (ratio-1))) * 120 + 120);
}

/*
// TODO: aggregate top-down instead of bottom-up to better resemble
// other flame graphs with the culprits on top instead of on bottom
void aggregateStack(TreeLeafItem* item, StackData* data)
{
    const QByteArray label = isBelowThreshold(item->label()) ? QByteArray() : functionInLabel(item->label());

    Frame& frame = (*data)[label];
    frame.cost = qMax(item->cost(), frame.cost);

    foreach(TreeLeafItem* child, item->children()) {
        aggregateStack(child, &frame.children);
    }
}*/

class FrameGraphicsItem : public QGraphicsRectItem
{
public:
    FrameGraphicsItem(const QRectF& rect, const quint64 cost, const QString& function)
        : QGraphicsRectItem(rect)
    {
        static const QString emptyLabel = QStringLiteral("???");

        m_label = i18nc("%1: memory cost, %2: function label",
                        "%2: %1",
                        cost,
                        function.isEmpty() ? emptyLabel : function);
        setToolTip(m_label);
    }

    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = 0)
    {
        QGraphicsRectItem::paint(painter, option, widget);

        // TODO: text should always be displayed in a constant size and not zoomed
        // TODO: items should only be scaled horizontally, not vertically
        // TODO: items should "fit" into the view width
        static QFontMetrics m(QFont(QStringLiteral("monospace")));
        const int margin = 5;
        const int width = rect().width() - 2 * margin;
        if (width < m.averageCharWidth() * 6) {
            return;
        }
        const int height = rect().height();

        const QPen oldPen = painter->pen();
        QPen pen = oldPen;
        pen.setColor(Qt::white);
        painter->setPen(pen);
        painter->drawText(margin + rect().x(), rect().y(), width, height, Qt::AlignCenter | Qt::TextSingleLine, m.elidedText(m_label, Qt::ElideRight, width));
        painter->setPen(oldPen);
    }

private:
    QString m_label;
};

// TODO: what is the right value for maxWidth here?
QVector<QGraphicsItem*> toGraphicsItems(const FlameGraphData::Stack& data,
                                        const qreal x_0 = 0, const qreal y_0 = 0,
                                        const qreal maxWidth = 800., qreal totalCostForColor = 0)
{
    QVector<QGraphicsItem*> ret;
    ret.reserve(data.size());

    double totalCost = 0;
    foreach(const auto& frame, data) {
        totalCost += frame.cost;
    }
    if (!totalCostForColor) {
        totalCostForColor = totalCost;
    }
    qDebug() << "graphicsitem:" << totalCost << totalCostForColor;

    qreal x = x_0;
    const qreal h = 25;
    const qreal y = y_0;

    const qreal x_margin = 0;
    const qreal y_margin = 2;

    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        const qreal w = maxWidth * double(it.value().cost) / totalCostForColor;
        FrameGraphicsItem* item = new FrameGraphicsItem(QRectF(x, y, w, h), it.value().cost, it.key());
        item->setBrush(color(it.value().cost, totalCostForColor));
        ret += toGraphicsItems(it.value().children, x, y - h - y_margin, w, totalCostForColor);
        x += w + x_margin;
        ret << item;
    }

    return ret;
}

}

FlameGraph::FlameGraph(QWidget* parent, Qt::WindowFlags flags)
    : QWidget(parent, flags)
    , m_scene(new QGraphicsScene(this))
    , m_view(new QGraphicsView(this))
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
    if (object == m_view->viewport() && event->type() == QEvent::Wheel) {
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
        if (wheelEvent->modifiers() == Qt::ControlModifier) {
            // zoom view with Ctrl + mouse wheel
            qreal scale = pow(1.1, double(wheelEvent->delta()) / (120.0 * 2.));
            m_view->scale(scale, scale);
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

    foreach(QGraphicsItem* item, toGraphicsItems(data.stack)) {
        m_scene->addItem(item);
    }

    qDebug() << m_scene->itemsBoundingRect() << m_scene->sceneRect() << m_view->rect() << m_view->contentsRect();
//     m_view->fitInView( m_scene->itemsBoundingRect(), Qt::KeepAspectRatio );
    // TODO: what is the correct scale value here?! without it, the contents in the view are teeny tiny!
//     m_view->scale(1, 1);

    qDebug() << "took me: " << t.elapsed();
}
