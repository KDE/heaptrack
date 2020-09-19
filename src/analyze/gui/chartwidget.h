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

#ifndef CHARTWIDGET_H
#define CHARTWIDGET_H

#include <QWidget>

class QRubberBand;
class ChartModel;

namespace KChart {
class Chart;
class CartesianAxis;
}

class QAbstractItemModel;

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

    void setIsFiltered(bool isFiltered);

signals:
    void selectionChanged(const Range& range);
    void filterRequested(int64_t minTime, int64_t maxTime);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateAxesTitle();
    void updateRubberBand();
    bool eventFilter(QObject* watched, QEvent* event) override;

    KChart::Chart* m_chart = nullptr;
    KChart::CartesianAxis* m_bottomAxis = nullptr;
    KChart::CartesianAxis* m_rightAxis = nullptr;
    ChartModel* m_model = nullptr;
    QRubberBand* m_rubberBand = nullptr;
    Range m_selection;
    bool m_isFiltered = false;
};

#endif // CHARTWIDGET_H
