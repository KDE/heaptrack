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

#include "chartwidget.h"

#include <QMenu>
#include <QPainter>
#include <QRubberBand>
#include <QTextStream>
#include <QToolTip>
#include <QVBoxLayout>

#include <KChartChart>
#include <KChartPlotter>

#include <KChartBackgroundAttributes>
#include <KChartCartesianCoordinatePlane>
#include <KChartDataValueAttributes>
#include <KChartFrameAttributes.h>
#include <KChartGridAttributes>
#include <KChartHeaderFooter>
#include <KChartLegend>

#include <KColorScheme>
#include <KLocalizedString>

#include "chartmodel.h"
#include "chartproxy.h"
#include "util.h"

#include <cmath>
#include <limits>

using namespace KChart;

namespace {
class TimeAxis : public CartesianAxis
{
    Q_OBJECT
public:
    explicit TimeAxis(AbstractCartesianDiagram* diagram = nullptr)
        : CartesianAxis(diagram)
    {
    }

    const QString customizedLabel(const QString& label) const override
    {
        const auto time = label.toLongLong();
        if (m_summaryData.filterParameters.isFiltered(m_summaryData.totalTime)) {
            return Util::formatTime(time) + QLatin1Char('\n')
                + Util::formatTime(time - m_summaryData.filterParameters.minTime);
        }
        return Util::formatTime(time);
    }

    void setSummaryData(const SummaryData& summaryData)
    {
        m_summaryData = summaryData;
        update();
    }

private:
    SummaryData m_summaryData;
};

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
        return Util::formatBytes(label.toLongLong());
    }
};

/// see also ProxyStyle which is responsible for unsetting SH_RubberBand_Mask
class ChartRubberBand : public QRubberBand
{
    Q_OBJECT
public:
    explicit ChartRubberBand(QWidget* parent)
        : QRubberBand(QRubberBand::Rectangle, parent)
    {
    }
    ~ChartRubberBand() = default;

protected:
    void paintEvent(QPaintEvent* event) override
    {
        auto brush = palette().highlight();
        if (brush != m_lastBrush) {
            auto color = brush.color();
            color.setAlpha(128);
            brush.setColor(color);
            m_cachedBrush = brush;
        } else {
            brush = m_cachedBrush;
        }

        QPainter painter(this);
        painter.fillRect(event->rect(), brush);
    }

private:
    QBrush m_lastBrush;
    QBrush m_cachedBrush;
};
}

ChartWidget::ChartWidget(QWidget* parent)
    : QWidget(parent)
    , m_chart(new Chart(this))
    , m_rubberBand(new ChartRubberBand(this))
{
    auto layout = new QVBoxLayout(this);
    layout->addWidget(m_chart);
    setLayout(layout);

    auto* coordinatePlane = dynamic_cast<CartesianCoordinatePlane*>(m_chart->coordinatePlane());
    Q_ASSERT(coordinatePlane);
    coordinatePlane->setAutoAdjustGridToZoom(true);

    m_chart->installEventFilter(this);

    m_chart->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_chart, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        if (!m_model)
            return;

        const auto isFiltered = m_summaryData.filterParameters.isFiltered(m_summaryData.totalTime);
        if (!m_selection && !isFiltered)
            return;

        auto* menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose, true);

        if (m_selection) {
            auto* reparse = menu->addAction(QIcon::fromTheme(QStringLiteral("timeline-use-zone-on")),
                                            i18n("Filter In On Selection"));
            connect(reparse, &QAction::triggered, this, [this]() {
                const auto startTime = std::min(m_selection.start, m_selection.end);
                const auto endTime = std::max(m_selection.start, m_selection.end);
                emit filterRequested(startTime, endTime);
            });
        }

        if (isFiltered) {
            auto* reset =
                menu->addAction(QIcon::fromTheme(QStringLiteral("timeline-use-zone-off")), i18n("Reset Filter"));
            connect(reset, &QAction::triggered, this,
                    [this]() { emit filterRequested(0, std::numeric_limits<int64_t>::max()); });
        }

        menu->popup(m_chart->mapToGlobal(point));
    });
}

ChartWidget::~ChartWidget() = default;

void ChartWidget::setSummaryData(const SummaryData& summaryData)
{
    m_summaryData = summaryData;
    updateAxesTitle();
    if (m_bottomAxis) {
        static_cast<TimeAxis*>(m_bottomAxis)->setSummaryData(summaryData);
    }
}

void ChartWidget::setModel(ChartModel* model, bool minimalMode)
{
    if (m_model == model)
        return;
    m_model = model;

    auto* coordinatePlane = dynamic_cast<CartesianCoordinatePlane*>(m_chart->coordinatePlane());
    Q_ASSERT(coordinatePlane);
    foreach (auto diagram, coordinatePlane->diagrams()) {
        coordinatePlane->takeDiagram(diagram);
        delete diagram;
    }

    if (minimalMode) {
        KChart::GridAttributes grid;
        grid.setSubGridVisible(false);
        coordinatePlane->setGlobalGridAttributes(grid);
    }

    switch (model->type()) {
    case ChartModel::Consumed:
        setToolTip(i18n("<qt>Shows the heap memory consumption over time.</qt>"));
        break;
    case ChartModel::Allocations:
        setToolTip(i18n("<qt>Shows number of memory allocations over time.</qt>"));
        break;
    case ChartModel::Temporary:
        setToolTip(i18n("<qt>Shows number of temporary memory allocations over time. "
                        "A temporary allocation is one that is followed immediately by its "
                        "corresponding deallocation, without other allocations happening "
                        "in-between.</qt>"));
        break;
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
        m_bottomAxis = new TimeAxis(totalPlotter);
        auto axisTextAttributes = m_bottomAxis->textAttributes();
        axisTextAttributes.setPen(foreground);
        m_bottomAxis->setTextAttributes(axisTextAttributes);
        auto axisTitleTextAttributes = m_bottomAxis->titleTextAttributes();
        axisTitleTextAttributes.setPen(foreground);
        auto fontSize = axisTitleTextAttributes.fontSize();
        fontSize.setCalculationMode(KChartEnums::MeasureCalculationModeAbsolute);
        if (minimalMode) {
            fontSize.setValue(font().pointSizeF() - 2);
        } else {
            fontSize.setValue(font().pointSizeF() + 2);
        }
        axisTitleTextAttributes.setFontSize(fontSize);
        m_bottomAxis->setTitleTextAttributes(axisTitleTextAttributes);
        m_bottomAxis->setPosition(CartesianAxis::Bottom);
        totalPlotter->addAxis(m_bottomAxis);

        m_rightAxis = model->type() == ChartModel::Allocations || model->type() == ChartModel::Temporary
            ? new CartesianAxis(totalPlotter)
            : new SizeAxis(totalPlotter);
        m_rightAxis->setTextAttributes(axisTextAttributes);
        m_rightAxis->setTitleTextAttributes(axisTitleTextAttributes);
        m_rightAxis->setPosition(CartesianAxis::Right);
        totalPlotter->addAxis(m_rightAxis);

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
    updateAxesTitle();
}

void ChartWidget::updateAxesTitle()
{
    if (!m_model)
        return;

    m_bottomAxis->setTitleText(m_model->headerData(0).toString());
    m_rightAxis->setTitleText(m_model->headerData(1).toString());

    if (m_summaryData.filterParameters.isFiltered(m_summaryData.totalTime)) {
        m_bottomAxis->setTitleText(
            i18n("%1 (filtered from %2 to %3, Δ%4)", m_bottomAxis->titleText(),
                 Util::formatTime(m_summaryData.filterParameters.minTime),
                 Util::formatTime(m_summaryData.filterParameters.maxTime),
                 Util::formatTime(m_summaryData.filterParameters.maxTime - m_summaryData.filterParameters.minTime)));
        m_rightAxis->setTitleText(i18n("%1 (filtered delta)", m_rightAxis->titleText()));
    }
}

QSize ChartWidget::sizeHint() const
{
    return {400, 50};
}

void ChartWidget::setSelection(const Range& selection)
{
    if (selection == m_selection || !m_model)
        return;

    m_selection = selection;

    const auto startTime = std::min(m_selection.start, m_selection.end);
    const auto endTime = std::max(m_selection.start, m_selection.end);

    const auto startCost = m_model->totalCostAt(startTime);
    const auto endCost = m_model->totalCostAt(endTime);

    QString toolTip;
    QTextStream stream(&toolTip);
    stream << "<qt><table cellpadding=2>";
    stream << i18n("<tr><th></th><th>Start</th><th>End</th><th>Delta</th></tr>");
    stream << i18n("<tr><th>Time</th><td>%1</td><td>%2</td><td>%3</td></tr>", Util::formatTime(startTime),
                   Util::formatTime(endTime), Util::formatTime(endTime - startTime));
    switch (m_model->type()) {
    case ChartModel::Consumed:
        stream << i18n("<tr><th>Consumed</th><td>%1</td><td>%2</td><td>%3</td></tr>", Util::formatBytes(startCost),
                       Util::formatBytes(endCost), Util::formatBytes(endCost - startCost));
        break;
    case ChartModel::Allocations:
        stream << i18n("<tr><th>Allocations</th><td>%1</td><td>%2</td><td>%3</td></tr>", startCost, endCost,
                       (endCost - startCost));
        break;
    case ChartModel::Temporary:
        stream << i18n("<tr><th>Temporary Allocations</th><td>%1</td><td>%2</td><td>%3</td></tr>", startCost, endCost,
                       (endCost - startCost));
        break;
    }
    stream << "</table></qt>";

    setToolTip(toolTip);
    updateRubberBand();

    emit selectionChanged(m_selection);
}

void ChartWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateRubberBand();
}

void ChartWidget::updateRubberBand()
{
    if (!m_selection || !m_model) {
        m_rubberBand->hide();
        return;
    }

    auto* coordinatePlane = static_cast<CartesianCoordinatePlane*>(m_chart->coordinatePlane());
    const auto delta = m_chart->pos().x();
    const auto pixelStart = coordinatePlane->translate({m_selection.start, 0}).x() + delta;
    const auto pixelEnd = coordinatePlane->translate({m_selection.end, 0}).x() + delta;
    auto selectionRect = QRect(QPoint(pixelStart, 0), QPoint(pixelEnd, height() - 1));
    m_rubberBand->setGeometry(selectionRect.normalized());
    m_rubberBand->show();
}

bool ChartWidget::eventFilter(QObject* watched, QEvent* event)
{
    Q_ASSERT(watched == m_chart);

    if (!m_model)
        return false;

    if (auto* mouseEvent = dynamic_cast<QMouseEvent*>(event)) {
        if (mouseEvent->buttons() == Qt::LeftButton) {
            auto* coordinatePlane = static_cast<CartesianCoordinatePlane*>(m_chart->coordinatePlane());
            const auto time = coordinatePlane->translateBack(mouseEvent->pos()).x();

            auto selection = m_selection;
            selection.end = time;
            if (event->type() == QEvent::MouseButtonPress) {
                selection.start = time;
            }

            setSelection(selection);
            QToolTip::showText(mouseEvent->globalPos(), toolTip(), this);
            return true;
        }
    }
    return false;
}

#include "chartwidget.moc"
