/*
 * Copyright 2015-2019 Milian Wolff <mail@milianw.de>
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

#include "flamegraph.h"

#include <cmath>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QStyleOption>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <KColorScheme>
#include <KLocalizedString>
#include <KStandardAction>
#include <ThreadWeaver/ThreadWeaver>

#include "util.h"

enum CostType
{
    Allocations,
    Temporary,
    Peak,
    Leaked,
};
Q_DECLARE_METATYPE(CostType)

namespace {
enum SearchMatchType
{
    NoSearch,
    NoMatch,
    DirectMatch,
    ChildMatch
};
}

class FrameGraphicsItem : public QGraphicsRectItem
{
public:
    FrameGraphicsItem(const qint64 cost, CostType costType, const Symbol& symbol, FrameGraphicsItem* parent = nullptr);
    FrameGraphicsItem(const qint64 cost, const Symbol& symbol, FrameGraphicsItem* parent);

    qint64 cost() const;
    void setCost(qint64 cost);
    Symbol symbol() const;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

    QString description() const;
    void setSearchMatchType(SearchMatchType matchType);

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    qint64 m_cost;
    Symbol m_symbol;
    CostType m_costType;
    bool m_isHovered;
    SearchMatchType m_searchMatch = NoSearch;
};

Q_DECLARE_METATYPE(FrameGraphicsItem*)

FrameGraphicsItem::FrameGraphicsItem(const qint64 cost, CostType costType, const Symbol& symbol,
                                     FrameGraphicsItem* parent)
    : QGraphicsRectItem(parent)
    , m_cost(cost)
    , m_symbol(symbol)
    , m_costType(costType)
    , m_isHovered(false)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    setAcceptHoverEvents(true);
}

FrameGraphicsItem::FrameGraphicsItem(const qint64 cost, const Symbol& symbol, FrameGraphicsItem* parent)
    : FrameGraphicsItem(cost, parent->m_costType, symbol, parent)
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

Symbol FrameGraphicsItem::symbol() const
{
    return m_symbol;
}

void FrameGraphicsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* /*widget*/)
{
    if (isSelected() || m_isHovered || m_searchMatch == DirectMatch) {
        auto selectedColor = brush().color();
        selectedColor.setAlpha(255);
        painter->fillRect(rect(), selectedColor);
    } else if (m_searchMatch == NoMatch) {
        auto noMatchColor = brush().color();
        noMatchColor.setAlpha(50);
        painter->fillRect(rect(), noMatchColor);
    } else { // default, when no search is running, or a sub-item is matched
        painter->fillRect(rect(), brush());
    }

    const QPen oldPen = painter->pen();
    auto pen = oldPen;
    if (m_searchMatch != NoMatch) {
        pen.setColor(brush().color());
        if (isSelected()) {
            pen.setWidth(2);
        }
        painter->setPen(pen);
        painter->drawRect(rect());
        painter->setPen(oldPen);
    }

    const int margin = 4;
    const int width = rect().width() - 2 * margin;
    if (width < option->fontMetrics.averageCharWidth() * 6) {
        // text is too wide for the current LOD, don't paint it
        return;
    }

    if (m_searchMatch == NoMatch) {
        auto color = oldPen.color();
        color.setAlpha(125);
        pen.setColor(color);
        painter->setPen(pen);
    }

    const int height = rect().height();
    painter->drawText(margin + rect().x(), rect().y(), width, height,
                      Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                      option->fontMetrics.elidedText(m_symbol.symbol, Qt::ElideRight, width));

    if (m_searchMatch == NoMatch) {
        painter->setPen(oldPen);
    }
}

void FrameGraphicsItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    QGraphicsRectItem::hoverEnterEvent(event);
    m_isHovered = true;
    update();
}

void FrameGraphicsItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    QGraphicsRectItem::hoverLeaveEvent(event);
    m_isHovered = false;
    update();
}

QString FrameGraphicsItem::description() const
{
    // we build the tooltip text on demand, which is much faster than doing that
    // for potentially thousands of items when we load the data
    const auto symbol = i18nc("%1: function, %2: binary", "%1 (%2)", m_symbol.symbol, m_symbol.binary);
    if (!parentItem()) {
        return m_symbol.symbol;
    }

    KFormat format;
    qint64 totalCost = 0;
    {
        auto item = this;
        while (item->parentItem()) {
            item = static_cast<const FrameGraphicsItem*>(item->parentItem());
        }
        totalCost = item->cost();
    }
    const auto fraction = Util::formatCostRelative(m_cost, totalCost);

    QString tooltip;
    switch (m_costType) {
    case Allocations:
        tooltip = i18nc("%1: number of allocations, %2: relative number, %3: function label",
                        "%1 (%2%) allocations in %3 and below.", m_cost, fraction, symbol);
        break;
    case Temporary:
        tooltip = i18nc("%1: number of temporary allocations, %2: relative number, "
                        "%3 function label",
                        "%1 (%2%) temporary allocations in %3 and below.", m_cost, fraction, symbol);
        break;
    case Peak:
        tooltip = i18nc("%1: peak consumption in bytes, %2: relative number, %3: "
                        "function label",
                        "%1 (%2%) contribution to peak consumption in %3 and below.",
                        format.formatByteSize(m_cost, 1, KFormat::MetricBinaryDialect), fraction, symbol);
        break;
    case Leaked:
        tooltip = i18nc("%1: leaked bytes, %2: relative number, %3: function label", "%1 (%2%) leaked in %3 and below.",
                        format.formatByteSize(m_cost, 1, KFormat::MetricBinaryDialect), fraction, symbol);
        break;
    }

    return tooltip;
}

void FrameGraphicsItem::setSearchMatchType(SearchMatchType matchType)
{
    if (m_searchMatch != matchType) {
        m_searchMatch = matchType;
        update();
    }
}

namespace {

/**
 * Generate a brush from the "mem" color space used in upstream FlameGraph.pl
 */
QBrush brush()
{
    // intern the brushes, to reuse them across items which can be thousands
    // otherwise we'd end up with dozens of allocations and higher memory
    // consumption
    static const QVector<QBrush> brushes = []() -> QVector<QBrush> {
        QVector<QBrush> brushes;
        std::generate_n(std::back_inserter(brushes), 100, []() {
            return QColor(0, 190 + 50 * qreal(rand()) / RAND_MAX, 210 * qreal(rand()) / RAND_MAX, 125);
        });
        return brushes;
    }();
    return brushes.at(rand() % brushes.size());
}

/**
 * Layout the flame graph and hide tiny items.
 */
void layoutItems(FrameGraphicsItem* parent)
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

FrameGraphicsItem* findItemBySymbol(const QList<QGraphicsItem*>& items, const Symbol& symbol)
{
    foreach (auto item_, items) {
        auto item = static_cast<FrameGraphicsItem*>(item_);
        if (item->symbol() == symbol) {
            return item;
        }
    }
    return nullptr;
}

/**
 * Convert the top-down graph into a tree of FrameGraphicsItem.
 */
void toGraphicsItems(const QVector<RowData>& data, FrameGraphicsItem* parent, int64_t AllocationData::*member,
                     const double costThreshold, bool collapseRecursion)
{
    foreach (const auto& row, data) {
        if (collapseRecursion && row.symbol.symbol != unresolvedFunctionName() && row.symbol == parent->symbol()) {
            toGraphicsItems(row.children, parent, member, costThreshold, collapseRecursion);
            continue;
        }
        auto item = findItemBySymbol(parent->childItems(), row.symbol);
        if (!item) {
            item = new FrameGraphicsItem(row.cost.*member, row.symbol, parent);
            item->setPen(parent->pen());
            item->setBrush(brush());
        } else {
            item->setCost(item->cost() + row.cost.*member);
        }
        if (item->cost() > costThreshold) {
            toGraphicsItems(row.children, item, member, costThreshold, collapseRecursion);
        }
    }
}

int64_t AllocationData::*memberForType(CostType type)
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
    }
    Q_UNREACHABLE();
}

FrameGraphicsItem* parseData(const QVector<RowData>& topDownData, CostType type, double costThreshold,
                             bool collapseRecursion)
{
    auto member = memberForType(type);

    double totalCost = 0;
    foreach (const auto& frame, topDownData) {
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
        label = i18n("%1 peak memory consumption", format.formatByteSize(totalCost, 1, KFormat::MetricBinaryDialect));
        break;
    case Leaked:
        label = i18n("%1 leaked in total", format.formatByteSize(totalCost, 1, KFormat::MetricBinaryDialect));
        break;
    }
    auto rootItem = new FrameGraphicsItem(totalCost, type, label);
    rootItem->setBrush(scheme.background());
    rootItem->setPen(pen);
    toGraphicsItems(topDownData, rootItem, member, totalCost * costThreshold / 100., collapseRecursion);
    return rootItem;
}

struct SearchResults
{
    SearchMatchType matchType = NoMatch;
    qint64 directCost = 0;
};

SearchResults applySearch(FrameGraphicsItem* item, const QString& searchValue)
{
    SearchResults result;
    if (searchValue.isEmpty()) {
        result.matchType = NoSearch;
    } else if (item->symbol().symbol.contains(searchValue, Qt::CaseInsensitive)
               || item->symbol().binary.contains(searchValue, Qt::CaseInsensitive)) {
        result.directCost += item->cost();
        result.matchType = DirectMatch;
    }

    // recurse into the child items, we always need to update all items
    for (auto* child : item->childItems()) {
        auto* childFrame = static_cast<FrameGraphicsItem*>(child);
        auto childMatch = applySearch(childFrame, searchValue);
        if (result.matchType != DirectMatch
            && (childMatch.matchType == DirectMatch || childMatch.matchType == ChildMatch)) {
            result.matchType = ChildMatch;
            result.directCost += childMatch.directCost;
        }
    }

    item->setSearchMatchType(result.matchType);
    return result;
}
}

FlameGraph::FlameGraph(QWidget* parent, Qt::WindowFlags flags)
    : QWidget(parent, flags)
    , m_costSource(new QComboBox(this))
    , m_scene(new QGraphicsScene(this))
    , m_view(new QGraphicsView(this))
    , m_displayLabel(new QLabel)
    , m_searchResultsLabel(new QLabel)
{
    qRegisterMetaType<FrameGraphicsItem*>();

    m_costSource->addItem(i18n("Memory Peak"), QVariant::fromValue(Peak));
    m_costSource->setItemData(2,
                              i18n("Show a flame graph over the contributions to the peak heap "
                                   "memory consumption of your application."),
                              Qt::ToolTipRole);
    m_costSource->addItem(i18n("Leaked"), QVariant::fromValue(Leaked));
    m_costSource->setItemData(3,
                              i18n("Show a flame graph over the leaked heap memory of your application. "
                                   "Memory is considered to be leaked when it never got deallocated. "),
                              Qt::ToolTipRole);
    m_costSource->addItem(i18n("Allocations"), QVariant::fromValue(Allocations));
    m_costSource->setItemData(0,
                              i18n("Show a flame graph over the number of allocations triggered by "
                                   "functions in your code."),
                              Qt::ToolTipRole);
    m_costSource->addItem(i18n("Temporary Allocations"), QVariant::fromValue(Temporary));
    m_costSource->setItemData(1,
                              i18n("Show a flame graph over the number of temporary allocations "
                                   "triggered by functions in your code. "
                                   "Allocations are marked as temporary when they are immediately "
                                   "followed by their deallocation."),
                              Qt::ToolTipRole);
    connect(m_costSource, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            &FlameGraph::showData);
    m_costSource->setToolTip(i18n("Select the data source that should be visualized in the flame graph."));

    m_scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    m_view->setScene(m_scene);
    m_view->viewport()->installEventFilter(this);
    m_view->viewport()->setMouseTracking(true);
    m_view->setFont(QFont(QStringLiteral("monospace")));

    auto bottomUpCheckbox = new QCheckBox(i18n("Bottom-Down View"), this);
    bottomUpCheckbox->setToolTip(i18n("Enable the bottom-down flame graph view. When this is unchecked, "
                                      "the top-down view is enabled by default."));
    bottomUpCheckbox->setChecked(m_showBottomUpData);
    connect(bottomUpCheckbox, &QCheckBox::toggled, this, [this, bottomUpCheckbox] {
        m_showBottomUpData = bottomUpCheckbox->isChecked();
        showData();
    });

    auto collapseRecursionCheckbox = new QCheckBox(i18n("Collapse Recursion"), this);
    collapseRecursionCheckbox->setChecked(m_collapseRecursion);
    collapseRecursionCheckbox->setToolTip(i18n("Collapse stack frames for functions calling themselves. "
                                               "When this is unchecked, recursive frames will be visualized "
                                               "separately."));
    connect(collapseRecursionCheckbox, &QCheckBox::toggled, this, [this, collapseRecursionCheckbox] {
        m_collapseRecursion = collapseRecursionCheckbox->isChecked();
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
                                   "Items with a relative cost below this value will not be shown in "
                                   "the flame graph. This is done as an optimization to quickly generate "
                                   "graphs for large data sets with low memory overhead. If you need more "
                                   "details, decrease the threshold value, or set it to zero.</qt>"));
    connect(costThreshold, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
            [this](double threshold) {
                m_costThreshold = threshold;
                showData();
            });

    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText(i18n("Search..."));
    m_searchInput->setToolTip(i18n("<qt>Search the flame graph for a symbol.</qt>"));
    m_searchInput->setClearButtonEnabled(true);
    connect(m_searchInput, &QLineEdit::textChanged, this, &FlameGraph::setSearchValue);

    auto controls = new QWidget(this);
    controls->setLayout(new QHBoxLayout);
    controls->layout()->addWidget(m_costSource);
    controls->layout()->addWidget(bottomUpCheckbox);
    controls->layout()->addWidget(collapseRecursionCheckbox);
    controls->layout()->addWidget(costThreshold);
    controls->layout()->addWidget(m_searchInput);

    m_displayLabel->setWordWrap(true);
    m_displayLabel->setTextInteractionFlags(m_displayLabel->textInteractionFlags() | Qt::TextSelectableByMouse);

    m_searchResultsLabel->setWordWrap(true);
    m_searchResultsLabel->setTextInteractionFlags(m_searchResultsLabel->textInteractionFlags()
                                                  | Qt::TextSelectableByMouse);
    m_searchResultsLabel->hide();

    setLayout(new QVBoxLayout);
    layout()->addWidget(controls);
    layout()->addWidget(m_view);
    layout()->addWidget(m_displayLabel);
    layout()->addWidget(m_searchResultsLabel);

    m_backAction = KStandardAction::back(this, SLOT(navigateBack()), this);
    addAction(m_backAction);
    m_forwardAction = KStandardAction::forward(this, SLOT(navigateForward()), this);
    addAction(m_forwardAction);
    m_resetAction = new QAction(QIcon::fromTheme(QStringLiteral("go-first")), i18n("Reset View"), this);
    m_resetAction->setShortcut(Qt::Key_Escape);
    connect(m_resetAction, &QAction::triggered, this, [this]() { selectItem(0); });
    addAction(m_resetAction);
    updateNavigationActions();
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        auto* menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose, true);
        if (auto item = static_cast<const FrameGraphicsItem*>(m_view->itemAt(point))) {
            auto* action = menu->addAction(i18n("View Caller/Callee"));
            connect(action, &QAction::triggered, this,
                    [this, item]() { emit callerCalleeViewRequested(item->symbol()); });
            menu->addSeparator();
        }
        menu->addActions(actions());
        menu->popup(m_view->mapToGlobal(point));
    });
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
                updateNavigationActions();
            }
        }
    } else if (event->type() == QEvent::MouseMove) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        auto item = static_cast<FrameGraphicsItem*>(m_view->itemAt(mouseEvent->pos()));
        setTooltipItem(item);
    } else if (event->type() == QEvent::Leave) {
        setTooltipItem(nullptr);
    } else if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
        if (!m_rootItem) {
            if (!m_buildingScene) {
                showData();
            }
        } else {
            selectItem(m_selectionHistory.at(m_selectedItem));
        }
        updateTooltip();
    } else if (event->type() == QEvent::ToolTip) {
        auto tooltip = m_displayLabel->toolTip();

        if (m_tooltipItem != m_view->itemAt(m_view->mapFromGlobal(QCursor::pos()))) {
            // don't show a tooltip when the cursor is in the empty region
            tooltip.clear();
        }

        if (tooltip.isEmpty()) {
            QToolTip::hideText();
        } else {
            QToolTip::showText(QCursor::pos(), QLatin1String("<qt>") + tooltip.toHtmlEscaped() + QLatin1String("</qt>"),
                               this);
        }
        event->accept();
        return true;
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

void FlameGraph::clearData()
{
    m_topDownData = {};
    m_bottomUpData = {};

    setData(nullptr);
}

void FlameGraph::showData()
{
    setData(nullptr);

    m_buildingScene = true;
    using namespace ThreadWeaver;
    auto data = m_showBottomUpData ? m_bottomUpData : m_topDownData;
    bool collapseRecursion = m_collapseRecursion;
    auto source = m_costSource->currentData().value<CostType>();
    auto threshold = m_costThreshold;
    stream() << make_job([data, source, threshold, collapseRecursion, this]() {
        auto parsedData = parseData(data, source, threshold, collapseRecursion);
        QMetaObject::invokeMethod(this, "setData", Qt::QueuedConnection, Q_ARG(FrameGraphicsItem*, parsedData));
    });
}

void FlameGraph::setTooltipItem(const FrameGraphicsItem* item)
{
    if (!item && m_selectedItem != -1 && m_selectionHistory.at(m_selectedItem)) {
        item = m_selectionHistory.at(m_selectedItem);
        m_view->setCursor(Qt::ArrowCursor);
    } else {
        m_view->setCursor(Qt::PointingHandCursor);
    }
    m_tooltipItem = item;
    updateTooltip();
}

void FlameGraph::updateTooltip()
{
    const auto text = m_tooltipItem ? m_tooltipItem->description() : QString();
    m_displayLabel->setToolTip(text);
    const auto metrics = m_displayLabel->fontMetrics();
    m_displayLabel->setText(metrics.elidedText(text, Qt::ElideRight, m_displayLabel->width()));
}

void FlameGraph::setData(FrameGraphicsItem* rootItem)
{
    m_scene->clear();
    m_buildingScene = false;
    m_tooltipItem = nullptr;
    m_rootItem = rootItem;
    m_selectionHistory.clear();
    m_selectionHistory.push_back(rootItem);
    m_selectedItem = 0;
    updateNavigationActions();
    if (!rootItem) {
        auto text = m_scene->addText(i18n("generating flame graph..."));
        m_view->centerOn(text);
        m_view->setCursor(Qt::BusyCursor);
        return;
    }

    m_view->setCursor(Qt::ArrowCursor);
    // layouting needs a root item with a given height, the rest will be
    // overwritten later
    rootItem->setRect(0, 0, 800, m_view->fontMetrics().height() + 4);
    m_scene->addItem(rootItem);

    if (!m_searchInput->text().isEmpty()) {
        setSearchValue(m_searchInput->text());
    }

    if (isVisible()) {
        selectItem(m_rootItem);
    }
}

void FlameGraph::selectItem(int item)
{
    m_selectedItem = item;
    updateNavigationActions();
    selectItem(m_selectionHistory.at(m_selectedItem));
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

    setTooltipItem(item);
}

void FlameGraph::setSearchValue(const QString& value)
{
    if (!m_rootItem) {
        return;
    }

    auto match = applySearch(m_rootItem, value);

    if (value.isEmpty()) {
        m_searchResultsLabel->hide();
    } else {
        QString label;
        KFormat format;
        const auto costFraction = Util::formatCostRelative(match.directCost, m_rootItem->cost());
        switch (m_costSource->currentData().value<CostType>()) {
        case Allocations:
        case Temporary:
            label = i18n("%1 (%2% of total of %3) allocations matched by search.", match.directCost, costFraction,
                         m_rootItem->cost());
            break;
        case Peak:
        case Leaked:
            label = i18n("%1 (%2% of total of %3) matched by search.",
                         format.formatByteSize(match.directCost, 1, KFormat::MetricBinaryDialect), costFraction,
                         format.formatByteSize(m_rootItem->cost(), 1, KFormat::MetricBinaryDialect));
            break;
        }
        m_searchResultsLabel->setText(label);
        m_searchResultsLabel->show();
    }
}

void FlameGraph::navigateBack()
{
    if (m_selectedItem > 0) {
        selectItem(m_selectedItem - 1);
    }
}

void FlameGraph::navigateForward()
{
    if ((m_selectedItem + 1) < m_selectionHistory.size()) {
        selectItem(m_selectedItem + 1);
    }
}

void FlameGraph::updateNavigationActions()
{
    m_backAction->setEnabled(m_selectedItem > 0);
    m_forwardAction->setEnabled(m_selectedItem + 1 < m_selectionHistory.size());
    m_resetAction->setEnabled(m_selectedItem > 0);
}
