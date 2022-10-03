/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef CHARTWIDGET_H
#define CHARTWIDGET_H

#include <QWidget>

#include "summarydata.h"

class QRubberBand;
class QAbstractItemModel;
class QSpinBox;

class ChartModel;

namespace KChart {
class Chart;
class CartesianAxis;
class Legend;
class Plotter;
}

class ChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ChartWidget(QWidget* parent = nullptr);
    virtual ~ChartWidget();

    void setModel(ChartModel* model, bool minimalMode = false);

    QSize sizeHint() const override;

    struct Range
    {
        float start = -1;
        float end = -1;

        bool operator==(const Range& rhs) const
        {
            return start == rhs.start && end == rhs.end;
        }

        explicit operator bool() const
        {
            return start != end;
        }
    };
    void setSelection(const Range& range);
    Range selection() const
    {
        return m_selection;
    }

    void setSummaryData(const SummaryData& summaryData);

signals:
    void selectionChanged(const Range& range);
    void filterRequested(int64_t minTime, int64_t maxTime);

public slots:
    void saveAs();

private:
    void updateToolTip();
    void updateStatusTip(qint64 time);
    void updateAxesTitle();
    void updateRubberBand();
    bool eventFilter(QObject* watched, QEvent* event) override;

    KChart::Plotter* m_totalPlotter = nullptr;
    KChart::Plotter* m_detailedPlotter = nullptr;
    QSpinBox* m_stackedDiagrams = nullptr;
    KChart::Chart* m_chart = nullptr;
    KChart::Legend* m_legend = nullptr;
    KChart::CartesianAxis* m_bottomAxis = nullptr;
    KChart::CartesianAxis* m_rightAxis = nullptr;
    ChartModel* m_model = nullptr;
    QRubberBand* m_rubberBand = nullptr;
    Range m_selection;
    SummaryData m_summaryData;
    QPixmap m_cachedChart;
};

#endif // CHARTWIDGET_H
