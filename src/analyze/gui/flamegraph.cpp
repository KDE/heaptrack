/*
 * Copyright 2015-2017 Milian Wolff <mail@milianw.de>
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

#include "flamegraph.h"

#include <cmath>

#include <QVBoxLayout>
#include <QGraphicsScene>
#include <QStyleOption>
#include <QGraphicsView>
#include <QLabel>
#include <QGraphicsRectItem>
#include <QWheelEvent>
#include <QEvent>
#include <QToolTip>
#include <QDebug>
#include <QAction>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QCursor>

#include <ThreadWeaver/ThreadWeaver>
#include <KLocalizedString>
#include <KColorScheme>

enum CostType
{
    Allocations,
    Temporary,
    Peak,
    Leaked,
    Allocated
};
Q_DECLARE_METATYPE(CostType)

class FrameGraphicsItem : public QGraphicsRectItem
{
public:
    FrameGraphicsItem(const qint64 cost, CostType costType, const QString& function, FrameGraphicsItem* parent = nullptr);
    FrameGraphicsItem(const qint64 cost, const QString& function, FrameGraphicsItem* parent);

    qint64 cost() const;
    void setCost(qint64 cost);
    QString function() const;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

    QString description() const;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

private:
    qint64 m_cost;
    QString m_function;
    CostType m_costType;
    bool m_isHovered;
};

Q_DECLARE_METATYPE(FrameGraphicsItem*)

FrameGraphicsItem::FrameGraphicsItem(const qint64 cost, CostType costType, const QString& function, FrameGraphicsItem* parent)
    : QGraphicsRectItem(parent)
    , m_cost(cost)
    , m_function(function)
    , m_costType(costType)
    , m_isHovered(false)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    setAcceptHoverEvents(true);
}

FrameGraphicsItem::FrameGraphicsItem(const qint64 cost, const QString& function, FrameGraphicsItem* parent)
    : FrameGraphicsItem(cost, parent->m_costType, function, parent)
{
}

qint64 FrameGraphicsItem::cost() const
{
    return m_cost;
}

void FrameGraphicsItem::setCost(qint64 cost)
{
    m_cost = cost;
}

QString FrameGraphicsItem::function() const
{
    return m_function;
}

void FrameGraphicsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* /*widget*/)
{
    if (isSelected() || m_isHovered) {
        auto selectedColor = brush().color();
        selectedColor.setAlpha(255);
        painter->fillRect(rect(), selectedColor);
    } else {
        painter->fillRect(rect(), brush());
    }

    const QPen oldPen = painter->pen();
    auto pen = oldPen;
    pen.setColor(brush().color());
    if (isSelected()) {
        pen.setWidth(2);
    }
    painter->setPen(pen);
    painter->drawRect(rect());
    painter->setPen(oldPen);

    const int margin = 4;
    const int width = rect().width() - 2 * margin;
    if (width < option->fontMetrics.averageCharWidth() * 6) {
        // text is too wide for the current LOD, don't paint it
        return;
    }

    const int height = rect().height();

    painter->drawText(margin + rect().x(), rect().y(), width, height, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                      option->fontMetrics.elidedText(m_function, Qt::ElideRight, width));
}

void FrameGraphicsItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    QGraphicsRectItem::hoverEnterEvent(event);
    m_isHovered = true;
}

QString FrameGraphicsItem::description() const
{
    // we build the tooltip text on demand, which is much faster than doing that for potentially thousands of items when we load the data
    QString tooltip;
    KFormat format;
    qint64 totalCost = 0;
    {
        auto item = this;
        while (item->parentItem()) {
            item = static_cast<const FrameGraphicsItem*>(item->parentItem());
        }
        totalCost = item->cost();
    }
    const auto fraction = QString::number(double(m_cost)  * 100. / totalCost, 'g', 3);
    const auto function = QString(QLatin1String("<span style='font-family:monospace'>") + m_function.toHtmlEscaped() + QLatin1String("</span>"));
    if (!parentItem()) {
        return function;
    }

    switch (m_costType) {
    case Allocations:
        tooltip = i18nc("%1: number of allocations, %2: relative number, %3: function label",
                        "%1 (%2%) allocations in %3 and below.", m_cost, fraction, function);
        break;
    case Temporary:
        tooltip = i18nc("%1: number of temporary allocations, %2: relative number, %3 function label",
                        "%1 (%2%) temporary allocations in %3 and below.", m_cost, fraction, function);
        break;
    case Peak:
        tooltip = i18nc("%1: peak consumption in bytes, %2: relative number, %3: function label",
                        "%1 (%2%) peak consumption in %3 and below.", format.formatByteSize(m_cost), fraction, function);
        break;
    case Leaked:
        tooltip = i18nc("%1: leaked bytes, %2: relative number, %3: function label",
                        "%1 (%2%) leaked in %3 and below.", format.formatByteSize(m_cost), fraction, function);
        break;
    case Allocated:
        tooltip = i18nc("%1: allocated bytes, %2: relative number, %3: function label",
                        "%1 (%2%) allocated in %3 and below.", format.formatByteSize(m_cost), fraction, function);
        break;
    }

    return tooltip;
}

void FrameGraphicsItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    QGraphicsRectItem::hoverLeaveEvent(event);
    m_isHovered = false;
}

namespace {

/**
 * Generate a brush from the "mem" color space used in upstream FlameGraph.pl
 */
QBrush brush()
{
    // intern the brushes, to reuse them across items which can be thousands
    // otherwise we'd end up with dozens of allocations and higher memory consumption
    static QVector<QBrush> brushes;
    if (brushes.isEmpty()) {
        std::generate_n(std::back_inserter(brushes), 100, [] () {
            return QColor(0, 190 + 50 * qreal(rand()) / RAND_MAX, 210 * qreal(rand()) / RAND_MAX, 125);
        });
    }
    return brushes.at(rand() % brushes.size());
}

/**
 * Layout the flame graph and hide tiny items.
 */
void layoutItems(FrameGraphicsItem *parent)
{
    const auto& parentRect = parent->rect();
    const auto pos = parentRect.topLeft();
    const qreal maxWidth = parentRect.width();
    const qreal h = parentRect.height();
    const qreal y_margin = 2.;
    const qreal y = pos.y() - h - y_margin;
    qreal x = pos.x();

    foreach (auto child, parent->childItems()) {
        auto frameChild = static_cast<FrameGraphicsItem*>(child);
        const qreal w = maxWidth * double(frameChild->cost()) / parent->cost();
        frameChild->setVisible(w > 1);
        if (frameChild->isVisible()) {
            frameChild->setRect(QRectF(x, y, w, h));
            layoutItems(frameChild);
            x += w;
        }
    }
}

FrameGraphicsItem* findItemByFunction(const QList<QGraphicsItem*>& items, const QString& function)
{
    foreach (auto item_, items) {
        auto item = static_cast<FrameGraphicsItem*>(item_);
        if (item->function() == function) {
            return item;
        }
    }
    return nullptr;
}

/**
 * Convert the top-down graph into a tree of FrameGraphicsItem.
 */
void toGraphicsItems(const QVector<RowData>& data, FrameGraphicsItem *parent, int64_t AllocationData::* member,
                     const double costThreshold)
{
    foreach (const auto& row, data) {
        auto item = findItemByFunction(parent->childItems(), row.location->function);
        if (!item) {
            item = new FrameGraphicsItem(row.cost.*member, row.location->function, parent);
            item->setPen(parent->pen());
            item->setBrush(brush());
        } else {
            item->setCost(item->cost() + row.cost.*member);
        }
        if (item->cost() > costThreshold) {
            toGraphicsItems(row.children, item, member, costThreshold);
        }
    }
}

int64_t AllocationData::* memberForType(CostType type)
{
    switch (type) {
    case Allocations:
        return &AllocationData::allocations;
    case Temporary:
        return &AllocationData::temporary;
    case Peak:
        return &AllocationData::peak;
    case Leaked:
        return &AllocationData::leaked;
    case Allocated:
        return &AllocationData::allocated;
    }
    Q_UNREACHABLE();
}

FrameGraphicsItem* parseData(const QVector<RowData>& topDownData, CostType type, double costThreshold)
{
    auto member = memberForType(type);

    double totalCost = 0;
    foreach(const auto& frame, topDownData) {
        totalCost += frame.cost.*member;
    }

    KColorScheme scheme(QPalette::Active);
    const QPen pen(scheme.foreground().color());

    KFormat format;
    QString label;
    switch (type) {
    case Allocations:
        label = i18n("%1 allocations in total", totalCost);
        break;
    case Temporary:
        label = i18n("%1 temporary allocations in total", totalCost);
        break;
    case Peak:
        label = i18n("%1 peak consumption in total", format.formatByteSize(totalCost));
        break;
    case Leaked:
        label = i18n("%1 leaked in total", format.formatByteSize(totalCost));
        break;
    case Allocated:
        label = i18n("%1 allocated in total", format.formatByteSize(totalCost));
        break;
    }
    auto rootItem = new FrameGraphicsItem(totalCost, type, label);
    rootItem->setBrush(scheme.background());
    rootItem->setPen(pen);
    toGraphicsItems(topDownData, rootItem, member, totalCost * costThreshold / 100.);
    return rootItem;
}

}

FlameGraph::FlameGraph(QWidget* parent, Qt::WindowFlags flags)
    : QWidget(parent, flags)
    , m_costSource(new QComboBox(this))
    , m_scene(new QGraphicsScene(this))
    , m_view(new QGraphicsView(this))
    , m_displayLabel(new QLabel)
    , m_rootItem(nullptr)
    , m_minRootWidth(0)
{
    qRegisterMetaType<FrameGraphicsItem*>();

    m_costSource->addItem(i18n("Allocations"), QVariant::fromValue(Allocations));
    m_costSource->setItemData(0, i18n("Show a flame graph over the number of allocations triggered by functions in your code."), Qt::ToolTipRole);
    m_costSource->addItem(i18n("Temporary Allocations"), QVariant::fromValue(Temporary));
    m_costSource->setItemData(1, i18n("Show a flame graph over the number of temporary allocations triggered by functions in your code. "
                                      "Allocations are marked as temporary when they are immediately followed by their deallocation."), Qt::ToolTipRole);
    m_costSource->addItem(i18n("Peak Consumption"), QVariant::fromValue(Peak));
    m_costSource->setItemData(2, i18n("Show a flame graph over the peak heap memory consumption of your application."), Qt::ToolTipRole);
    m_costSource->addItem(i18n("Leaked"), QVariant::fromValue(Leaked));
    m_costSource->setItemData(3, i18n("Show a flame graph over the leaked heap memory of your application. "
                                      "Memory is considered to be leaked when it never got deallocated. "), Qt::ToolTipRole);
    m_costSource->addItem(i18n("Allocated"), QVariant::fromValue(Allocated));
    m_costSource->setItemData(4, i18n("Show a flame graph over the total memory allocated by functions in your code. "
                                      "This aggregates all memory allocations and ignores deallocations."), Qt::ToolTipRole);
    connect(m_costSource, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
            this, &FlameGraph::showData);
    m_costSource->setToolTip(i18n("Select the data source that should be visualized in the flame graph."));

    m_scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    m_view->setScene(m_scene);
    m_view->viewport()->installEventFilter(this);
    m_view->viewport()->setMouseTracking(true);
    m_view->setFont(QFont(QStringLiteral("monospace")));
    m_view->setContextMenuPolicy(Qt::ActionsContextMenu);

    auto bottomUpCheckbox = new QCheckBox(i18n("Bottom-Down View"), this);
    bottomUpCheckbox->setToolTip(i18n("Enable the bottom-down flame graph view. When this is unchecked, the top-down view is enabled by default."));
    connect(bottomUpCheckbox, &QCheckBox::toggled, this, [this, bottomUpCheckbox] {
        m_showBottomUpData = bottomUpCheckbox->isChecked();
        showData();
    });

    auto costThreshold = new QDoubleSpinBox(this);
    costThreshold->setDecimals(2);
    costThreshold->setMinimum(0);
    costThreshold->setMaximum(99.90);
    costThreshold->setPrefix(i18n("Cost Threshold: "));
    costThreshold->setSuffix(QStringLiteral("%"));
    costThreshold->setValue(m_costThreshold);
    costThreshold->setSingleStep(0.01);
    costThreshold->setToolTip(i18n("<qt>The cost threshold defines a fractional cut-off value. "
                                   "Items with a relative cost below this value will not be shown in the flame graph. "
                                   "This is done as an optimization to quickly generate graphs for large data sets with "
                                   "low memory overhead. If you need more details, decrease the threshold value, or set it to zero.</qt>"));
    connect(costThreshold, static_cast<void (QDoubleSpinBox::*) (double)>(&QDoubleSpinBox::valueChanged),
            this, [this] (double threshold) {
                m_costThreshold = threshold;
                showData();
            });

    m_displayLabel->setWordWrap(true);
    m_displayLabel->setTextInteractionFlags(m_displayLabel->textInteractionFlags() | Qt::TextSelectableByMouse);

    auto controls = new QWidget(this);
    controls->setLayout(new QHBoxLayout);
    controls->layout()->addWidget(m_costSource);
    controls->layout()->addWidget(bottomUpCheckbox);
    controls->layout()->addWidget(costThreshold);

    setLayout(new QVBoxLayout);
    layout()->addWidget(controls);
    layout()->addWidget(m_view);
    layout()->addWidget(m_displayLabel);

    {
        auto action = new QAction(tr("back"), this);
        action->setShortcuts({QKeySequence::Back, Qt::Key_Backspace});
        connect(action, &QAction::triggered,
                this, &FlameGraph::navigateBack);
        addAction(action);
    }
    {
        auto action = new QAction(tr("forward"), this);
        action->setShortcuts(QKeySequence::Forward);
        connect(action, &QAction::triggered,
                this, &FlameGraph::navigateForward);
        addAction(action);
    }
}

FlameGraph::~FlameGraph() = default;

bool FlameGraph::eventFilter(QObject* object, QEvent* event)
{
    bool ret = QObject::eventFilter(object, event);

    if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            auto item = static_cast<FrameGraphicsItem*>(m_view->itemAt(mouseEvent->pos()));
            if (item && item != m_selectionHistory.at(m_selectedItem)) {
                selectItem(item);
                if (m_selectedItem != m_selectionHistory.size() - 1) {
                    m_selectionHistory.remove(m_selectedItem + 1, m_selectionHistory.size() - m_selectedItem - 1);
                }
                m_selectedItem = m_selectionHistory.size();
                m_selectionHistory.push_back(item);
            }
        }
    } else if (event->type() == QEvent::MouseMove) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        auto item = static_cast<FrameGraphicsItem*>(m_view->itemAt(mouseEvent->pos()));
        if (item) {
            setDisplayText(item->description());
        } else {
            setDisplayText({});
        }
    } else if (event->type() == QEvent::Leave) {
        setDisplayText({});
    } else if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
        if (!m_rootItem) {
            showData();
        } else {
            selectItem(m_selectionHistory.at(m_selectedItem));
        }
    } else if (event->type() == QEvent::Hide) {
        setData(nullptr);
    }
    return ret;
}

void FlameGraph::setTopDownData(const TreeData& topDownData)
{
    m_topDownData = topDownData;

    if (isVisible()) {
        showData();
    }
}

void FlameGraph::setBottomUpData(const TreeData& bottomUpData)
{
    m_bottomUpData = bottomUpData;
}

void FlameGraph::showData()
{
    setData(nullptr);

    using namespace ThreadWeaver;
    auto data = m_showBottomUpData ? m_bottomUpData : m_topDownData;
    auto source = m_costSource->currentData().value<CostType>();
    auto threshold = m_costThreshold;
    stream() << make_job([data, source, threshold, this]() {
        auto parsedData = parseData(data, source, threshold);
        QMetaObject::invokeMethod(this, "setData", Qt::QueuedConnection,
                                  Q_ARG(FrameGraphicsItem*, parsedData));
    });
}

void FlameGraph::setDisplayText(const QString& text)
{
    if (text.isEmpty() && m_selectedItem != -1 && m_selectionHistory.at(m_selectedItem)) {
        m_displayLabel->setText(m_selectionHistory.at(m_selectedItem)->description());
        m_view->setCursor(Qt::ArrowCursor);
    } else {
        m_displayLabel->setText(text);
        m_view->setCursor(Qt::PointingHandCursor);
    }
}

void FlameGraph::setData(FrameGraphicsItem* rootItem)
{
    m_scene->clear();
    m_rootItem = rootItem;
    m_selectionHistory.clear();
    m_selectionHistory.push_back(rootItem);
    m_selectedItem = 0;
    if (!rootItem) {
        auto text = m_scene->addText(i18n("generating flame graph..."));
        m_view->centerOn(text);
        m_view->setCursor(Qt::BusyCursor);
        return;
    }

    m_view->setCursor(Qt::ArrowCursor);
    // layouting needs a root item with a given height, the rest will be overwritten later
    rootItem->setRect(0, 0, 800, m_view->fontMetrics().height() + 4);
    m_scene->addItem(rootItem);

    if (isVisible()) {
        selectItem(m_rootItem);
    }
}

void FlameGraph::selectItem(FrameGraphicsItem* item)
{
    if (!item) {
        return;
    }

    // scale item and its parents to the maximum available width
    // also hide all siblings of the parent items
    const auto rootWidth = m_view->viewport()->width() - 40;
    auto parent = item;
    while (parent) {
        auto rect = parent->rect();
        rect.setLeft(0);
        rect.setWidth(rootWidth);
        parent->setRect(rect);
        if (parent->parentItem()) {
            foreach (auto sibling, parent->parentItem()->childItems()) {
                sibling->setVisible(sibling == parent);
            }
        }
        parent = static_cast<FrameGraphicsItem*>(parent->parentItem());
    }

    // then layout all items below the selected on
    layoutItems(item);

    // and make sure it's visible
    m_view->centerOn(item);

    setDisplayText(item->description());
}

void FlameGraph::navigateBack()
{
    if (m_selectedItem > 0) {
        --m_selectedItem;
    }
    selectItem(m_selectionHistory.at(m_selectedItem));
}

void FlameGraph::navigateForward()
{
    if ((m_selectedItem + 1) < m_selectionHistory.size()) {
        ++m_selectedItem;
    }
    selectItem(m_selectionHistory.at(m_selectedItem));
}
