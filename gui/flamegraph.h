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
#include <QMap>

class QGraphicsScene;
class QGraphicsView;

struct FlameGraphData
{
    struct Frame {
        quint64 cost = 0;
        using Stack = QMap<QString, Frame>;
        Stack children;
    };
    using Stack = Frame::Stack;

    Stack stack;
};

Q_DECLARE_METATYPE(FlameGraphData);

class FlameGraph : public QWidget
{
    Q_OBJECT
public:
    FlameGraph(QWidget* parent = 0, Qt::WindowFlags flags = 0);
    ~FlameGraph();

    void setData(const FlameGraphData& data);

protected:
    virtual bool eventFilter(QObject* object, QEvent* event);

private:
    QGraphicsScene* m_scene;
    QGraphicsView* m_view;
    FlameGraphData m_data;
};

#endif // FLAMEGRAPH_H
