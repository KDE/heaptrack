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

#include "chartmodel.h"
#include "chartproxy.h"

using namespace KChart;

namespace {
class TimeAxis : public CartesianAxis
{
    Q_OBJECT
public:
    explicit TimeAxis(AbstractCartesianDiagram* diagram = nullptr)
        : CartesianAxis(diagram)
    {}

    const QString customizedLabel(const QString& label) const override
    {
        // squeeze large numbers here
        return QString::number(label.toDouble() / 1000, 'g', 2) + QLatin1Char('s');
    }
};

class SizeAxis : public CartesianAxis
{
    Q_OBJECT
public:
    explicit SizeAxis(AbstractCartesianDiagram* diagram = nullptr)
        : CartesianAxis(diagram)
    {}

    const QString customizedLabel(const QString& label) const override
    {
        // TODO: change distance between labels to 1024 and simply use prettyCost() here
        KFormat format(QLocale::system());
        return format.formatByteSize(label.toDouble(), 1, KFormat::MetricBinaryDialect);
    }
};
}

ChartWidget::ChartWidget(QWidget* parent)
    : QWidget(parent)
    , m_chart(new Chart(this))
{
    auto layout = new QVBoxLayout(this);
    layout->addWidget(m_chart);
    setLayout(layout);
}

ChartWidget::~ChartWidget() = default;

void ChartWidget::setModel(ChartModel* model)
{
    auto* coordinatePlane = dynamic_cast<CartesianCoordinatePlane*>(m_chart->coordinatePlane());
    Q_ASSERT(coordinatePlane);
    foreach (auto diagram, coordinatePlane->diagrams()) {
        coordinatePlane->takeDiagram(diagram);
        delete diagram;
    }

    {
        auto totalPlotter = new Plotter(this);
        totalPlotter->setAntiAliasing(true);
        auto totalProxy = new ChartProxy(true, this);
        totalProxy->setSourceModel(model);
        totalPlotter->setModel(totalProxy);
        totalPlotter->setType(Plotter::Stacked);

        KColorScheme scheme(QPalette::Active, KColorScheme::Window);
        const QPen foreground(scheme.foreground().color());
        auto bottomAxis = new TimeAxis(totalPlotter);
        auto axisTextAttributes = bottomAxis->textAttributes();
        axisTextAttributes.setPen(foreground);
        bottomAxis->setTextAttributes(axisTextAttributes);
        auto axisTitleTextAttributes = bottomAxis->titleTextAttributes();
        axisTitleTextAttributes.setPen(foreground);
        bottomAxis->setTitleTextAttributes(axisTitleTextAttributes);
        bottomAxis->setTitleText(model->headerData(0).toString());
        bottomAxis->setPosition(CartesianAxis::Bottom);
        totalPlotter->addAxis(bottomAxis);

        CartesianAxis* rightAxis = model->type() == ChartModel::Allocations ? new CartesianAxis(totalPlotter) : new SizeAxis(totalPlotter);
        rightAxis->setTextAttributes(axisTextAttributes);
        rightAxis->setTitleTextAttributes(axisTitleTextAttributes);
        rightAxis->setTitleText(model->headerData(1).toString());
        rightAxis->setPosition(CartesianAxis::Right);
        totalPlotter->addAxis(rightAxis);

        coordinatePlane->addDiagram(totalPlotter);
    }

    {
        auto plotter = new Plotter(this);
        plotter->setAntiAliasing(true);
        plotter->setType(Plotter::Stacked);

        auto proxy = new ChartProxy(false, this);
        proxy->setSourceModel(model);
        plotter->setModel(proxy);
        coordinatePlane->addDiagram(plotter);
    }

}

#include "chartwidget.moc"
