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

#include "chartwidget.h"

#include <QVBoxLayout>

#include <KChartChart>
#include <KChartPlotter>

#include <KChartGridAttributes>
#include <KChartHeaderFooter>
#include <KChartCartesianCoordinatePlane>
#include <KChartLegend>
#include <KChartDataValueAttributes>
#include <KChartBackgroundAttributes>
#include <KChartFrameAttributes.h>

#include <KFormat>
#include <KLocalizedString>
#include <KColorScheme>

#include "chartproxy.h"

using namespace KChart;

namespace {
class TimeAxis : public CartesianAxis
{
    Q_OBJECT
public:
    explicit TimeAxis(AbstractCartesianDiagram* diagram = 0)
        : CartesianAxis(diagram)
    {}

    virtual const QString customizedLabel(const QString& label) const
    {
        // squeeze large numbers here
        return QString::number(label.toDouble() / 1000, 'g', 2) + QLatin1Char('s');
    }
};

class SizeAxis : public CartesianAxis
{
    Q_OBJECT
public:
    explicit SizeAxis(AbstractCartesianDiagram* diagram = 0)
        : CartesianAxis(diagram)
    {}

    virtual const QString customizedLabel(const QString& label) const
    {
        // TODO: change distance between labels to 1024 and simply use prettyCost() here
        KFormat format(QLocale::system());
        return format.formatByteSize(label.toDouble(), 1, KFormat::MetricBinaryDialect);
    }
};
}

ChartWidget::ChartWidget(QWidget* parent)
    : QWidget(parent)
    , m_chart(new KChart::Chart(this))
    , m_plotter(new Plotter(this))
{
    auto layout = new QVBoxLayout(this);
    layout->addWidget(m_chart);
    setLayout(layout);

    m_plotter->setAntiAliasing(true);
    m_plotter->setType(KChart::Plotter::Stacked);

    auto* coordinatePlane = dynamic_cast<CartesianCoordinatePlane*>(m_chart->coordinatePlane());
    Q_ASSERT(coordinatePlane);
    coordinatePlane->addDiagram(m_plotter);
}

ChartWidget::~ChartWidget() = default;

void ChartWidget::setModel(ChartModel* model, ChartModel::Columns costColumn)
{
    if (m_plotter->model()) {
        delete m_plotter->model();
    }

    Q_ASSERT(costColumn != ChartModel::TimeStampColumn);
    auto proxy = new ChartProxy(costColumn, this);
    proxy->setSourceModel(model);

    foreach (auto axis, m_plotter->axes()) {
        m_plotter->takeAxis(axis);
        delete axis;
    }

    KColorScheme scheme(QPalette::Active, KColorScheme::Window);
    const QPen foreground(scheme.foreground().color());
    auto bottomAxis = new TimeAxis(m_plotter);
    auto axisTextAttributes = bottomAxis->textAttributes();
    axisTextAttributes.setPen(foreground);
    bottomAxis->setTextAttributes(axisTextAttributes);
    auto axisTitleTextAttributes = bottomAxis->titleTextAttributes();
    axisTitleTextAttributes.setPen(foreground);
    bottomAxis->setTitleTextAttributes(axisTitleTextAttributes);
    bottomAxis->setTitleText(model->headerData(ChartModel::TimeStampColumn).toString());
    bottomAxis->setPosition(KChart::CartesianAxis::Bottom);
    m_plotter->addAxis(bottomAxis);

    CartesianAxis* rightAxis = costColumn == ChartModel::AllocationsColumn ? new CartesianAxis(m_plotter) : new SizeAxis(m_plotter);
    rightAxis->setTextAttributes(axisTextAttributes);
    rightAxis->setTitleTextAttributes(axisTitleTextAttributes);
    rightAxis->setTitleText(model->headerData(costColumn).toString());
    rightAxis->setPosition(CartesianAxis::Right);
    m_plotter->addAxis(rightAxis);

    m_plotter->setModel(proxy);
}

#include "chartwidget.moc"
