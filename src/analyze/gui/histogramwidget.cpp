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

#include "histogramwidget.h"

#include <QSortFilterProxyModel>
#include <QVBoxLayout>

#include <KChartBarDiagram>
#include <KChartChart>

#include <KChartBackgroundAttributes>
#include <KChartCartesianCoordinatePlane>
#include <KChartDataValueAttributes>
#include <KChartFrameAttributes.h>
#include <KChartGridAttributes>
#include <KChartHeaderFooter>
#include <KChartLegend>

#include <KColorScheme>
#include <KFormat>
#include <KLocalizedString>

#include "histogrammodel.h"

using namespace KChart;

namespace {
class SizeAxis : public CartesianAxis
{
    Q_OBJECT
public:
    explicit SizeAxis(AbstractCartesianDiagram* diagram = nullptr)
        : CartesianAxis(diagram)
    {
    }

    const QString customizedLabel(const QString& label) const override
    {
        KFormat format(QLocale::system());
        return format.formatByteSize(label.toDouble(), 1, KFormat::MetricBinaryDialect);
    }
};

class HistogramProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit HistogramProxy(bool showTotal, QObject* parent = nullptr)
        : QSortFilterProxyModel(parent)
        , m_showTotal(showTotal)
    {
    }
    virtual ~HistogramProxy() = default;

protected:
    bool filterAcceptsColumn(int sourceColumn, const QModelIndex& /*sourceParent*/) const override
    {
        if (m_showTotal) {
            return sourceColumn == 0;
        } else {
            return sourceColumn != 0;
        }
    }

private:
    bool m_showTotal;
};
}

HistogramWidget::HistogramWidget(QWidget* parent)
    : QWidget(parent)
    , m_chart(new KChart::Chart(this))
    , m_total(new BarDiagram(this))
    , m_detailed(new BarDiagram(this))
{
    auto layout = new QVBoxLayout(this);
    layout->addWidget(m_chart);
    setLayout(layout);

    auto* coordinatePlane = dynamic_cast<CartesianCoordinatePlane*>(m_chart->coordinatePlane());
    Q_ASSERT(coordinatePlane);

    {
        m_total->setAntiAliasing(true);

        KColorScheme scheme(QPalette::Active, KColorScheme::Window);
        QPen foreground(scheme.foreground().color());
        auto bottomAxis = new CartesianAxis(m_total);
        auto axisTextAttributes = bottomAxis->textAttributes();
        axisTextAttributes.setPen(foreground);
        bottomAxis->setTextAttributes(axisTextAttributes);
        auto axisTitleTextAttributes = bottomAxis->titleTextAttributes();
        axisTitleTextAttributes.setPen(foreground);
        bottomAxis->setTitleTextAttributes(axisTitleTextAttributes);
        bottomAxis->setPosition(KChart::CartesianAxis::Bottom);
        bottomAxis->setTitleText(i18n("Requested Allocation Size"));
        m_total->addAxis(bottomAxis);

        auto* rightAxis = new CartesianAxis(m_total);
        rightAxis->setTextAttributes(axisTextAttributes);
        rightAxis->setTitleTextAttributes(axisTitleTextAttributes);
        rightAxis->setTitleText(i18n("Number of Allocations"));
        rightAxis->setPosition(CartesianAxis::Right);
        m_total->addAxis(rightAxis);

        coordinatePlane->addDiagram(m_total);

        m_total->setType(BarDiagram::Normal);
    }

    {
        m_detailed->setAntiAliasing(true);

        coordinatePlane->addDiagram(m_detailed);

        m_detailed->setType(BarDiagram::Stacked);
    }
}

HistogramWidget::~HistogramWidget() = default;

void HistogramWidget::setModel(QAbstractItemModel* model)
{
    {
        auto proxy = new HistogramProxy(true, this);
        proxy->setSourceModel(model);
        m_total->setModel(proxy);
    }
    {
        auto proxy = new HistogramProxy(false, this);
        proxy->setSourceModel(model);
        m_detailed->setModel(proxy);
    }
}

#include "histogramwidget.moc"
