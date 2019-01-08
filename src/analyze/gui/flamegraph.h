/*
 * Copyright 2015-2017 Milian Wolff <mail@milianw.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef FLAMEGRAPH_H
#define FLAMEGRAPH_H

#include <QVector>
#include <QWidget>

#include "treemodel.h"

class QGraphicsScene;
class QGraphicsView;
class QComboBox;
class QLabel;
class QLineEdit;

class FrameGraphicsItem;

class FlameGraph : public QWidget
{
    Q_OBJECT
public:
    FlameGraph(QWidget* parent = nullptr, Qt::WindowFlags flags = 0);
    ~FlameGraph();

    void setTopDownData(const TreeData& topDownData);
    void setBottomUpData(const TreeData& bottomUpData);

    void clearData();

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private slots:
    void setData(FrameGraphicsItem* rootItem);
    void setSearchValue(const QString& value);
    void navigateBack();
    void navigateForward();

signals:
    void callerCalleeViewRequested(const Symbol& symbol);

private:
    void setTooltipItem(const FrameGraphicsItem* item);
    void updateTooltip();
    void showData();
    void selectItem(int item);
    void selectItem(FrameGraphicsItem* item);
    void updateNavigationActions();

    TreeData m_topDownData;
    TreeData m_bottomUpData;

    QComboBox* m_costSource;
    QGraphicsScene* m_scene;
    QGraphicsView* m_view;
    QLabel* m_displayLabel;
    QLabel* m_searchResultsLabel;
    QLineEdit* m_searchInput = nullptr;
    QAction* m_forwardAction = nullptr;
    QAction* m_backAction = nullptr;
    QAction* m_resetAction = nullptr;
    const FrameGraphicsItem* m_tooltipItem = nullptr;
    FrameGraphicsItem* m_rootItem = nullptr;
    QVector<FrameGraphicsItem*> m_selectionHistory;
    int m_selectedItem = -1;
    int m_minRootWidth = 0;
    bool m_showBottomUpData = false;
    bool m_collapseRecursion = true;
    bool m_buildingScene = false;
    // cost threshold in percent, items below that value will not be shown
    double m_costThreshold = 0.1;
};

#endif // FLAMEGRAPH_H
