/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef HISTOGRAMWIDGET_H
#define HISTOGRAMWIDGET_H

#include <QWidget>

namespace KChart {
class Chart;
class BarDiagram;
}

class QAbstractItemModel;

class HistogramWidget : public QWidget
{
    Q_OBJECT
public:
    explicit HistogramWidget(QWidget* parent = nullptr);
    virtual ~HistogramWidget();

    void setModel(QAbstractItemModel* model);

private:
    KChart::Chart* m_chart;
    KChart::BarDiagram* m_total;
    KChart::BarDiagram* m_detailed;
};

#endif // HISTOGRAMWIDGET_H
