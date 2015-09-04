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

#ifndef FLAMEGRAPH_H
#define FLAMEGRAPH_H

#include <QWidget>
#include <QGraphicsRectItem>
#include "bottomupmodel.h"

class QGraphicsScene;
class QGraphicsView;

class FrameGraphicsItem : public QGraphicsRectItem
{
public:
    FrameGraphicsItem(const quint64 cost, const QString& function, FrameGraphicsItem* parent = nullptr);

    quint64 cost() const;

    static QFont font();
    static QFontMetrics fontMetrics();
    static int margin();
    static int itemHeight();

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

private:
    QString m_label;
    quint64 m_cost;
    bool m_isHovered;
};

class FlameGraph : public QWidget
{
    Q_OBJECT
public:
    FlameGraph(QWidget* parent = nullptr, Qt::WindowFlags flags = 0);
    ~FlameGraph();

    void setData(FrameGraphicsItem* rootFrame);
    // called from background thread
    static FrameGraphicsItem* parseData(const QVector<RowData>& data);

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    void zoomInto(FrameGraphicsItem* item);

    QGraphicsScene* m_scene;
    QGraphicsView* m_view;
    FrameGraphicsItem* m_rootItem;
    int m_minRootWidth;
};

#endif // FLAMEGRAPH_H
