/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
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
class QPushButton;

class FrameGraphicsItem;

class FlameGraph : public QWidget
{
    Q_OBJECT
public:
    FlameGraph(QWidget* parent = nullptr);
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
    QPushButton* m_backButton = nullptr;
    QPushButton* m_forwardButton = nullptr;
    const FrameGraphicsItem* m_tooltipItem = nullptr;
    FrameGraphicsItem* m_rootItem = nullptr;
    QVector<FrameGraphicsItem*> m_selectionHistory;
    int m_selectedItem = -1;
    bool m_showBottomUpData = false;
    bool m_collapseRecursion = true;
    bool m_templateElision = false;
    bool m_buildingScene = false;
    // cost threshold in percent, items below that value will not be shown
    double m_costThreshold = 0.1;
};

#endif // FLAMEGRAPH_H
