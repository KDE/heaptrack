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
        // TODO: when the unit is 'b' also use prettyCost() here
        return QString::number(label.toDouble());
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

    KColorScheme scheme(QPalette::Active, KColorScheme::Window);
    QPen foreground(scheme.foreground().color());
    auto bottomAxis = new TimeAxis(m_plotter);
    auto axisTextAttributes = bottomAxis->textAttributes();
    axisTextAttributes.setPen(foreground);
    bottomAxis->setTextAttributes(axisTextAttributes);
    auto axisTitleTextAttributes = bottomAxis->titleTextAttributes();
    axisTitleTextAttributes.setPen(foreground);
    bottomAxis->setTitleTextAttributes(axisTitleTextAttributes);
    bottomAxis->setTitleText(i18n("time in ms"));
    bottomAxis->setPosition(KChart::CartesianAxis::Bottom);
    m_plotter->addAxis(bottomAxis);

    auto* rightAxis = new SizeAxis(m_plotter);
    rightAxis->setTextAttributes(axisTextAttributes);
    rightAxis->setTitleTextAttributes(axisTitleTextAttributes);
    rightAxis->setTitleText(i18n("memory heap size"));
    rightAxis->setPosition(CartesianAxis::Right);
    m_plotter->addAxis(rightAxis);

    auto* coordinatePlane = dynamic_cast<CartesianCoordinatePlane*>(m_chart->coordinatePlane());
    Q_ASSERT(coordinatePlane);
    coordinatePlane->addDiagram(m_plotter);
}

ChartWidget::~ChartWidget() = default;

void ChartWidget::setModel(QAbstractItemModel* model)
{
    m_plotter->setModel(model);
}

#include "chartwidget.moc"
