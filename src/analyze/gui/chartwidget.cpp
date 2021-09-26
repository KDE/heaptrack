/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "chartwidget.h"

#include <QApplication>
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
        if (m_summaryData.filterParameters.isFilteredByTime(m_summaryData.totalTime)) {
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
    connect(coordinatePlane, &CartesianCoordinatePlane::needUpdate, this, &ChartWidget::updateRubberBand);

    m_chart->setCursor(Qt::IBeamCursor);
    m_chart->setMouseTracking(true);
    m_chart->installEventFilter(this);

    m_chart->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_chart, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        if (!m_model)
            return;

        const auto isFiltered = m_summaryData.filterParameters.isFilteredByTime(m_summaryData.totalTime);
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

    {
        auto totalPlotter = new Plotter(this);
        totalPlotter->setAntiAliasing(true);
        auto totalProxy = new ChartProxy(true, this);
        totalProxy->setSourceModel(model);
        totalPlotter->setModel(totalProxy);
        totalPlotter->setType(Plotter::Stacked);

        KColorScheme scheme(QPalette::Active, KColorScheme::Window);
        const QPen foreground(scheme.foreground().color());
        auto fixupTextAttributes = [&foreground](KChart::TextAttributes attributes, float pointSize) {
            attributes.setPen(foreground);
            auto fontSize = attributes.fontSize();
            fontSize.setAbsoluteValue(pointSize);
            attributes.setFontSize(fontSize);
            return attributes;
        };

        m_bottomAxis = new TimeAxis(totalPlotter);
        const auto axisTextAttributes = fixupTextAttributes(m_bottomAxis->textAttributes(), font().pointSizeF() - 2);
        m_bottomAxis->setTextAttributes(axisTextAttributes);
        const auto axisTitleTextAttributes =
            fixupTextAttributes(m_bottomAxis->titleTextAttributes(), font().pointSizeF() + (minimalMode ? (-2) : (+2)));
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

    updateToolTip();
    updateAxesTitle();
}

void ChartWidget::updateToolTip()
{
    if (!m_model)
        return;

    const auto startTime = std::min(m_selection.start, m_selection.end);
    const auto endTime = std::max(m_selection.start, m_selection.end);

    const auto startCost = m_model->totalCostAt(startTime);
    const auto endCost = m_model->totalCostAt(endTime);

    QString toolTip;
    if (!qFuzzyCompare(startTime, endTime)) {
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
            stream << i18n("<tr><th>Temporary Allocations</th><td>%1</td><td>%2</td><td>%3</td></tr>", startCost,
                           endCost, (endCost - startCost));
            break;
        }
        stream << "</table></qt>";
    } else {
        switch (m_model->type()) {
        case ChartModel::Consumed:
            toolTip = i18n("<qt>Shows the heap memory consumption over time.<br>Click and drag to select a time range "
                           "for filtering.</qt>");
            break;
        case ChartModel::Allocations:
            toolTip = i18n("<qt>Shows number of memory allocations over time.<br>Click and drag to select a time range "
                           "for filtering.</qt>");
            break;
        case ChartModel::Temporary:
            toolTip = i18n("<qt>Shows number of temporary memory allocations over time. "
                           "A temporary allocation is one that is followed immediately by its "
                           "corresponding deallocation, without other allocations happening "
                           "in-between.<br>Click and drag to select a time range for filtering.</qt>");
            break;
        }
    }

    setToolTip(toolTip);
}

void ChartWidget::updateAxesTitle()
{
    if (!m_model)
        return;

    m_bottomAxis->setTitleText(m_model->headerData(0).toString());
    m_rightAxis->setTitleText(m_model->headerData(1).toString());

    if (m_summaryData.filterParameters.isFilteredByTime(m_summaryData.totalTime)) {
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

    updateToolTip();
    updateRubberBand();

    emit selectionChanged(m_selection);
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

    auto mapPosToTime = [this](const QPointF& pos) {
        auto* coordinatePlane = static_cast<CartesianCoordinatePlane*>(m_chart->coordinatePlane());
        return coordinatePlane->translateBack(pos).x();
    };

    if (auto* mouseEvent = dynamic_cast<QMouseEvent*>(event)) {
        if (mouseEvent->button() == Qt::LeftButton || mouseEvent->buttons() == Qt::LeftButton) {
            const auto time = mapPosToTime(mouseEvent->localPos());

            auto selection = m_selection;
            selection.end = time;
            if (event->type() == QEvent::MouseButtonPress) {
                selection.start = time;
                m_chart->setCursor(Qt::SizeHorCursor);
                m_cachedChart = m_chart->grab();
            } else if (event->type() == QEvent::MouseButtonRelease) {
                m_chart->setCursor(Qt::IBeamCursor);
                m_cachedChart = {};
            }

            setSelection(selection);
            QToolTip::showText(mouseEvent->globalPos(), toolTip(), this);
            return true;
        } else if (event->type() == QEvent::MouseMove && !mouseEvent->buttons()) {
            updateStatusTip(mapPosToTime(mouseEvent->localPos()));
        }
    } else if (event->type() == QEvent::Paint && !m_cachedChart.isNull()) {
        // use the cached chart while interacting with the rubber band
        // otherwise, use the normal paint even as that one is required for
        // the mouse mapping etc. to work correctly...
        QPainter painter(m_chart);
        painter.drawPixmap(m_chart->rect(), m_cachedChart);
        return true;
    }
    return false;
}

void ChartWidget::updateStatusTip(qint64 time)
{
    if (!m_model)
        return;

    const auto text = [=]() {
        if (time < 0 || time > m_summaryData.filterParameters.maxTime) {
            return i18n("Click and drag to select time range for filtering.");
        }

        const auto cost = m_model->totalCostAt(time);
        switch (m_model->type()) {
        case ChartModel::Consumed:
            return i18n("T = %1, Consumed: %2. Click and drag to select time range for filtering.",
                        Util::formatTime(time), Util::formatBytes(cost));
            break;
        case ChartModel::Allocations:
            return i18n("T = %1, Allocations: %2. Click and drag to select time range for filtering.",
                        Util::formatTime(time), cost);
            break;
        case ChartModel::Temporary:
            return i18n("T = %1, Temporary Allocations: %2. Click and drag to select time range for filtering.",
                        Util::formatTime(time), cost);
            break;
        }
        Q_UNREACHABLE();
    }();
    setStatusTip(text);

    // force update
    QStatusTipEvent event(text);
    QApplication::sendEvent(this, &event);
}

#include "chartwidget.moc"
