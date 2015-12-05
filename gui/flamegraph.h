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
#include "treemodel.h"

class QGraphicsScene;
class QGraphicsView;

class FrameGraphicsItem;

class FlameGraph : public QWidget
{
    Q_OBJECT
public:
    FlameGraph(QWidget* parent = nullptr, Qt::WindowFlags flags = 0);
    ~FlameGraph();

    void setTopDownData(const TreeData& topDownData);
    void setBottomUpData(const TreeData& bottomUpData);

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private slots:
    void setData(FrameGraphicsItem* rootItem);

private:
    void showData();
    void selectItem(FrameGraphicsItem* item);

    TreeData m_topDownData;
    TreeData m_bottomUpData;

    QGraphicsScene* m_scene;
    QGraphicsView* m_view;
    FrameGraphicsItem* m_rootItem;
    FrameGraphicsItem* m_selectedItem;
    int m_minRootWidth;
    bool m_showBottomUpData = false;
};

#endif // FLAMEGRAPH_H
