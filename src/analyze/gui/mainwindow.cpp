/*
    SPDX-FileCopyrightText: 2015-2019 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "mainwindow.h"

#include <ui_mainwindow.h>

#include <cmath>

#include <KConfigGroup>
#include <KLazyLocalizedString>
#include <KLocalizedString>
#include <KShell>
#include <KStandardAction>

#include <QAction>
#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QFileDialog>
#include <QMenu>
#include <QShortcut>
#include <QStatusBar>
#include <QProcess>
#include <QInputDialog>

#include "analyze/suppressions.h"

#include "callercalleemodel.h"
#include "costdelegate.h"
#include "costheaderview.h"
#include "parser.h"
#include "stacksmodel.h"
#include "suppressionsmodel.h"
#include "topproxy.h"
#include "treemodel.h"
#include "treeproxy.h"

#include "gui_config.h"

#if KChart_FOUND
#include "chartmodel.h"
#include "chartproxy.h"
#include "chartwidget.h"
#include "histogrammodel.h"
#include "histogramwidget.h"
#endif

using namespace std;

namespace {
const int MAINWINDOW_VERSION = 1;

namespace Config {
namespace Groups {
const char MainWindow[] = "MainWindow";
const char CodeNavigation[] = "CodeNavigation";
}
namespace Entries {
const char State[] = "State";
const char CustomCommand[] = "CustomCommand";
const char IDE[] = "IDE";
}
}

struct IdeSettings
{
    const char* const app;
    const char* const args;
    const KLazyLocalizedString name;
};

static const IdeSettings ideSettings[] = {
    {"kdevelop", "%f:%l:%c", kli18n("KDevelop")},
    {"kate", "%f --line %l --column %c", kli18n("Kate")},
    {"kwrite", "%f --line %l --column %c", kli18n("KWrite")},
    {"gedit", "%f +%l:%c", kli18n("gedit")},
    {"gvim", "%f +%l", kli18n("gvim")},
    {"qtcreator", "-client %f:%l", kli18n("Qt Creator")}
};
static const int ideSettingsSize = sizeof(ideSettings) / sizeof(IdeSettings);

bool isAppAvailable(const char* app)
{
    return !QStandardPaths::findExecutable(QString::fromUtf8(app)).isEmpty();
}

int firstAvailableIde()
{
    for (int i = 0; i < ideSettingsSize; ++i) {
        if (isAppAvailable(ideSettings[i].app)) {
            return i;
        }
    }
    return -1;
}

template <typename T>
void setupContextMenu(QTreeView* view, T callback)
{
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(view, &QTreeView::customContextMenuRequested, view, [view, callback](const QPoint& point) {
        const auto index = view->indexAt(point);
        if (!index.isValid()) {
            return;
        }

        callback(index);
    });
}

template <typename T>
void setupTreeContextMenu(QTreeView* view, T callback)
{
    setupContextMenu(view, [callback](const QModelIndex& index) {
        QMenu contextMenu;
        auto* viewCallerCallee = contextMenu.addAction(i18n("View Caller/Callee"));
        auto* action = contextMenu.exec(QCursor::pos());
        if (action == viewCallerCallee) {
            const auto symbol = index.data(TreeModel::SymbolRole).value<Symbol>();

            if (symbol.isValid()) {
                callback(symbol);
            }
        }
    });
}

void addLocationContextMenu(QTreeView* treeView, MainWindow* window)
{
    treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(treeView, &QTreeView::customContextMenuRequested, treeView, [treeView, window](const QPoint& pos) {
        auto index = treeView->indexAt(pos);
        if (!index.isValid()) {
            return;
        }
        const auto resultData = index.data(SourceMapModel::ResultDataRole).value<const ResultData*>();
        Q_ASSERT(resultData);
        const auto location = index.data(SourceMapModel::LocationRole).value<FileLine>();
        const auto file = resultData->string(location.fileId);
        if (!QFile::exists(file)) {
            return;
        }
        auto menu = new QMenu(treeView);
        auto openFile =
            new QAction(QIcon::fromTheme(QStringLiteral("document-open")), i18n("Open file in editor"), menu);
        QObject::connect(openFile, &QAction::triggered, openFile,
                         [file, line = location.line, window] { window->navigateToCode(file, line); });
        menu->addAction(openFile);
        menu->popup(treeView->mapToGlobal(pos));
    });
    QObject::connect(treeView, &QTreeView::activated, window, [window](const QModelIndex& index) {
        const auto resultData = index.data(SourceMapModel::ResultDataRole).value<const ResultData*>();
        Q_ASSERT(resultData);
        const auto location = index.data(SourceMapModel::LocationRole).value<FileLine>();
        const auto file = resultData->string(location.fileId);
        if (QFile::exists(file))
            window->navigateToCode(file, location.line);
    });
}

Qt::SortOrder defaultSortOrder(QAbstractItemModel* model, int column)
{
    auto initialSortOrder = model->headerData(column, Qt::Horizontal, Qt::InitialSortOrderRole);
    if (initialSortOrder.canConvert<Qt::SortOrder>())
        return initialSortOrder.value<Qt::SortOrder>();
    return Qt::AscendingOrder;
}

void sortByColumn(QTreeView* view, int column)
{
    view->sortByColumn(column, defaultSortOrder(view->model(), column));
}

template <typename T>
void setupTopView(TreeModel* source, QTreeView* view, TopProxy::Type type, T callback)
{
    auto proxy = new TopProxy(type, source);
    proxy->setSourceModel(source);
    proxy->setSortRole(TreeModel::SortRole);
    view->setModel(proxy);
    sortByColumn(view, 1);
    view->header()->setStretchLastSection(true);
    setupTreeContextMenu(view, callback);
}

#if KChart_FOUND
ChartWidget* addChartTab(QTabWidget* tabWidget, const QString& title, ChartModel::Type type, const Parser* parser,
                         void (Parser::*dataReady)(const ChartData&), MainWindow* window)
{
    auto tab = new ChartWidget(tabWidget->parentWidget());
    QObject::connect(parser, &Parser::summaryAvailable, tab, &ChartWidget::setSummaryData);
    tabWidget->addTab(tab, title);
    tabWidget->setTabEnabled(tabWidget->indexOf(tab), false);
    auto model = new ChartModel(type, tab);
    tab->setModel(model);
    QObject::connect(parser, dataReady, tab, [=](const ChartData& data) {
        model->resetData(data);
        tabWidget->setTabEnabled(tabWidget->indexOf(tab), true);
    });
    QObject::connect(window, &MainWindow::clearData, model, &ChartModel::clearData);
    QObject::connect(window, &MainWindow::clearData, tab, [tab]() { tab->setSelection({}); });
    QObject::connect(tab, &ChartWidget::filterRequested, window, &MainWindow::reparse);
    return tab;
}
#endif

template <typename T>
void setupTreeModel(TreeModel* model, QTreeView* view, CostDelegate* costDelegate, QLineEdit* filterFunction,
                    QLineEdit* filterModule, T callback)
{
    auto proxy = new TreeProxy(TreeModel::SymbolRole, TreeModel::ResultDataRole, model);
    proxy->setSourceModel(model);
    proxy->setSortRole(TreeModel::SortRole);

    view->setModel(proxy);
    sortByColumn(view, TreeModel::PeakColumn);
    view->setItemDelegateForColumn(TreeModel::PeakColumn, costDelegate);
    view->setItemDelegateForColumn(TreeModel::LeakedColumn, costDelegate);
    view->setItemDelegateForColumn(TreeModel::AllocationsColumn, costDelegate);
    view->setItemDelegateForColumn(TreeModel::TemporaryColumn, costDelegate);
    view->setHeader(new CostHeaderView(view));

    QObject::connect(filterFunction, &QLineEdit::textChanged, proxy, &TreeProxy::setFunctionFilter);
    QObject::connect(filterModule, &QLineEdit::textChanged, proxy, &TreeProxy::setModuleFilter);
    setupTreeContextMenu(view, callback);
}

void setupCallerCallee(CallerCalleeModel* model, QTreeView* view, QLineEdit* filterFunction, QLineEdit* filterModule)
{
    auto costDelegate = new CostDelegate(CallerCalleeModel::SortRole, CallerCalleeModel::TotalCostRole, view);
    auto callerCalleeProxy = new TreeProxy(CallerCalleeModel::SymbolRole, CallerCalleeModel::ResultDataRole, model);
    callerCalleeProxy->setSourceModel(model);
    callerCalleeProxy->setSortRole(CallerCalleeModel::SortRole);
    view->setModel(callerCalleeProxy);
    sortByColumn(view, CallerCalleeModel::InclusivePeakColumn);
    view->setItemDelegateForColumn(CallerCalleeModel::SelfPeakColumn, costDelegate);
    view->setItemDelegateForColumn(CallerCalleeModel::SelfLeakedColumn, costDelegate);
    view->setItemDelegateForColumn(CallerCalleeModel::SelfAllocationsColumn, costDelegate);
    view->setItemDelegateForColumn(CallerCalleeModel::SelfTemporaryColumn, costDelegate);
    view->setItemDelegateForColumn(CallerCalleeModel::InclusivePeakColumn, costDelegate);
    view->setItemDelegateForColumn(CallerCalleeModel::InclusiveLeakedColumn, costDelegate);
    view->setItemDelegateForColumn(CallerCalleeModel::InclusiveAllocationsColumn, costDelegate);
    view->setItemDelegateForColumn(CallerCalleeModel::InclusiveTemporaryColumn, costDelegate);
    view->setHeader(new CostHeaderView(view));
    QObject::connect(filterFunction, &QLineEdit::textChanged, callerCalleeProxy, &TreeProxy::setFunctionFilter);
    QObject::connect(filterModule, &QLineEdit::textChanged, callerCalleeProxy, &TreeProxy::setModuleFilter);
}

template <typename Model>
Model* setupModelAndProxyForView(QTreeView* view)
{
    auto model = new Model(view);
    auto proxy = new QSortFilterProxyModel(model);
    proxy->setSourceModel(model);
    proxy->setSortRole(Model::SortRole);
    view->setModel(proxy);
    sortByColumn(view, Model::InitialSortColumn);
    auto costDelegate = new CostDelegate(Model::SortRole, Model::TotalCostRole, view);
    for (int i = 1; i < Model::NUM_COLUMNS; ++i) {
        view->setItemDelegateForColumn(i, costDelegate);
    }

    view->setHeader(new CostHeaderView(view));

    return model;
}

template <typename Model, typename Handler>
void connectCallerOrCalleeModel(QTreeView* view, CallerCalleeModel* callerCalleeCostModel, Handler handler)
{
    QObject::connect(view, &QTreeView::activated, view, [callerCalleeCostModel, handler](const QModelIndex& index) {
        const auto symbol = index.data(Model::SymbolRole).template value<Symbol>();
        auto sourceIndex = callerCalleeCostModel->indexForKey(symbol);
        handler(sourceIndex);
    });
}

QString insertWordWrapMarkers(QString text)
{
    // insert zero-width spaces after every 50 word characters to enable word wrap in the middle of words
    static const QRegularExpression pattern(QStringLiteral("(\\w{50})"));
    return text.replace(pattern, QStringLiteral("\\1\u200B"));
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_ui(new Ui::MainWindow)
    , m_parser(new Parser(this))
    , m_config(KSharedConfig::openConfig(QStringLiteral("heaptrack_gui")))
{
    m_ui->setupUi(this);

    auto group = m_config->group(Config::Groups::MainWindow);
    auto state = group.readEntry(Config::Entries::State, QByteArray());
    restoreState(state, MAINWINDOW_VERSION);

    m_ui->pages->setCurrentWidget(m_ui->openPage);
    // TODO: proper progress report
    m_ui->loadingProgress->setMinimum(0);
    m_ui->loadingProgress->setMaximum(1000); // range is set as 0 to 1000 for fractional % bar display
    m_ui->loadingProgress->setValue(0);

    auto bottomUpModel = new TreeModel(this);
    auto topDownModel = new TreeModel(this);
    auto callerCalleeModel = new CallerCalleeModel(this);
    connect(this, &MainWindow::clearData, bottomUpModel, &TreeModel::clearData);
    connect(this, &MainWindow::clearData, topDownModel, &TreeModel::clearData);
    connect(this, &MainWindow::clearData, callerCalleeModel, &CallerCalleeModel::clearData);
    connect(this, &MainWindow::clearData, m_ui->flameGraphTab, &FlameGraph::clearData);

    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->callerCalleeTab), false);
    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->topDownTab), false);
    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->flameGraphTab), false);

    auto* suppressionsModel = new SuppressionsModel(this);
    {
        auto* proxy = new QSortFilterProxyModel(this);
        proxy->setSourceModel(suppressionsModel);
        m_ui->suppressionsView->setModel(proxy);
        auto* delegate = new CostDelegate(SuppressionsModel::SortRole, SuppressionsModel::TotalCostRole, this);
        m_ui->suppressionsView->setItemDelegateForColumn(static_cast<int>(SuppressionsModel::Columns::Leaked),
                                                         delegate);
        m_ui->suppressionsView->setItemDelegateForColumn(static_cast<int>(SuppressionsModel::Columns::Matches),
                                                         delegate);

        auto margins = m_ui->suppressionBox->contentsMargins();
        margins.setLeft(0);
        m_ui->suppressionBox->setContentsMargins(margins);
    }

    connect(m_parser, &Parser::bottomUpDataAvailable, this, [=](const TreeData& data) {
        bottomUpModel->resetData(data);
        if (!m_diffMode) {
            m_ui->flameGraphTab->setBottomUpData(data);
        }
        m_ui->progressLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
        statusBar()->addWidget(m_ui->progressLabel, 1);
        statusBar()->addWidget(m_ui->loadingProgress);
        m_ui->pages->setCurrentWidget(m_ui->resultsPage);
        m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->bottomUpTab), true);
    });
    connect(m_parser, &Parser::callerCalleeDataAvailable, this, [=](const CallerCalleeResults& data) {
        callerCalleeModel->setResults(data);
        m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->callerCalleeTab), true);
    });
    connect(m_parser, &Parser::topDownDataAvailable, this, [=](const TreeData& data) {
        topDownModel->resetData(data);
        m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->topDownTab), true);
        if (!m_diffMode) {
            m_ui->flameGraphTab->setTopDownData(data);
        }
        m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->flameGraphTab), !m_diffMode);
    });
    connect(m_parser, &Parser::summaryAvailable, this, [=](const SummaryData& data) {
        bottomUpModel->setSummary(data);
        topDownModel->setSummary(data);
        suppressionsModel->setSuppressions(data);
        m_ui->suppressionBox->setVisible(suppressionsModel->rowCount() > 0);
        const auto isFiltered = data.filterParameters.isFilteredByTime(data.totalTime);
        QString textLeft;
        QString textCenter;
        QString textRight;
        {
            QTextStream stream(&textLeft);
            const auto debuggee = insertWordWrapMarkers(data.debuggee);
            stream << "<qt><dl>"
                   << (data.fromAttached ? i18n("<dt><b>debuggee</b>:</dt><dd "
                                                "style='font-family:monospace;'>%1 <i>(attached)</i></dd>",
                                                debuggee)
                                         : i18n("<dt><b>debuggee</b>:</dt><dd "
                                                "style='font-family:monospace;'>%1</dd>",
                                                debuggee));
            if (isFiltered) {
                stream << i18n("<dt><b>total runtime</b>:</dt><dd>%1, filtered from %2 to %3 (%4)</dd>",
                               Util::formatTime(data.totalTime), Util::formatTime(data.filterParameters.minTime),
                               Util::formatTime(data.filterParameters.maxTime),
                               Util::formatTime(data.filterParameters.maxTime - data.filterParameters.minTime));
            } else {
                stream << i18n("<dt><b>total runtime</b>:</dt><dd>%1</dd>", Util::formatTime(data.totalTime));
            }
            stream << i18n("<dt><b>total system memory</b>:</dt><dd>%1</dd>", Util::formatBytes(data.totalSystemMemory))
                   << "</dl></qt>";
        }
        {
            QTextStream stream(&textCenter);
            const double totalTimeS = 0.001 * (data.filterParameters.maxTime - data.filterParameters.minTime);
            stream << "<qt><dl>"
                   << i18n("<dt><b>calls to allocation functions</b>:</dt><dd>%1 "
                           "(%2/s)</dd>",
                           data.cost.allocations, qint64(data.cost.allocations / totalTimeS))
                   << i18n("<dt><b>temporary allocations</b>:</dt><dd>%1 (%2%, "
                           "%3/s)</dd>",
                           data.cost.temporary,
                           std::round(float(data.cost.temporary) * 100.f * 100.f / data.cost.allocations) / 100.f,
                           qint64(data.cost.temporary / totalTimeS))
                   << "</dl></qt>";
        }
        {
            QTextStream stream(&textRight);
            stream << "<qt><dl>"
                   << i18n("<dt><b>peak heap memory consumption</b>:</dt><dd>%1 "
                           "after %2</dd>",
                           Util::formatBytes(data.cost.peak), Util::formatTime(data.peakTime))
                   << i18n("<dt><b>peak RSS</b> (including heaptrack "
                           "overhead):</dt><dd>%1</dd>",
                           Util::formatBytes(data.peakRSS));
            if (isFiltered) {
                stream << i18n("<dt><b>memory consumption delta</b>:</dt><dd>%1</dd>",
                               Util::formatBytes(data.cost.leaked));
            } else {
                if (data.totalLeakedSuppressed) {
                    stream << i18n("<dt><b>total memory leaked</b>:</dt><dd>%1 (%2 suppressed)</dd>",
                                   Util::formatBytes(data.cost.leaked), Util::formatBytes(data.totalLeakedSuppressed));
                } else {
                    stream << i18n("<dt><b>total memory leaked</b>:</dt><dd>%1</dd>",
                                   Util::formatBytes(data.cost.leaked));
                }
            }
            stream << "</dl></qt>";
        }

        m_ui->summaryLeft->setText(textLeft);
        m_ui->summaryCenter->setText(textCenter);
        m_ui->summaryRight->setText(textRight);
        m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->summaryTab), true);
    });
    connect(m_parser, &Parser::progressMessageAvailable, m_ui->progressLabel, &QLabel::setText);
    connect(m_parser, &Parser::progress, m_ui->loadingProgress, &QProgressBar::setValue);
    auto removeProgress = [this] {
        auto layout = qobject_cast<QVBoxLayout*>(m_ui->loadingPage->layout());
        Q_ASSERT(layout);
        const auto idx = layout->indexOf(m_ui->loadingLabel) + 1;
        layout->insertWidget(idx, m_ui->loadingProgress);
        layout->insertWidget(idx + 1, m_ui->progressLabel);
        m_ui->progressLabel->setAlignment(Qt::AlignVCenter | Qt::AlignHCenter);
        m_closeAction->setEnabled(true);
        m_openAction->setEnabled(true);
    };
    connect(m_parser, &Parser::finished, this, removeProgress);
    connect(m_parser, &Parser::failedToOpen, this, [this, removeProgress](const QString& failedFile) {
        removeProgress();
        m_ui->pages->setCurrentWidget(m_ui->openPage);
        showError(i18n("Failed to parse file %1.", failedFile));
    });
    m_ui->messages->hide();

#if KChart_FOUND
    auto consumedTab = addChartTab(m_ui->tabWidget, i18n("Consumed"), ChartModel::Consumed, m_parser,
                                   &Parser::consumedChartDataAvailable, this);
    auto allocationsTab = addChartTab(m_ui->tabWidget, i18n("Allocations"), ChartModel::Allocations, m_parser,
                                      &Parser::allocationsChartDataAvailable, this);
    auto temporaryAllocationsTab = addChartTab(m_ui->tabWidget, i18n("Temporary Allocations"), ChartModel::Temporary,
                                               m_parser, &Parser::temporaryChartDataAvailable, this);
    auto syncSelection = [=](const ChartWidget::Range& selection) {
        consumedTab->setSelection(selection);
        allocationsTab->setSelection(selection);
        temporaryAllocationsTab->setSelection(selection);
    };
    connect(consumedTab, &ChartWidget::selectionChanged, syncSelection);
    connect(allocationsTab, &ChartWidget::selectionChanged, syncSelection);
    connect(temporaryAllocationsTab, &ChartWidget::selectionChanged, syncSelection);

    auto sizesTab = new HistogramWidget(this);
    m_ui->tabWidget->addTab(sizesTab, i18n("Sizes"));
    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(sizesTab), false);
    auto sizeHistogramModel = new HistogramModel(this);
    sizesTab->setModel(sizeHistogramModel);
    connect(this, &MainWindow::clearData, sizeHistogramModel, &HistogramModel::clearData);

    connect(m_parser, &Parser::sizeHistogramDataAvailable, this, [=](const HistogramData& data) {
        sizeHistogramModel->resetData(data);
        m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(sizesTab), true);
    });
#endif

    auto calleesModel = setupModelAndProxyForView<CalleeModel>(m_ui->calleeView);
    auto callersModel = setupModelAndProxyForView<CallerModel>(m_ui->callerView);
    auto sourceMapModel = setupModelAndProxyForView<SourceMapModel>(m_ui->locationView);

    auto selectCallerCaleeeIndex = [callerCalleeModel, calleesModel, callersModel, sourceMapModel,
                                    this](const QModelIndex& index) {
        const auto resultData = callerCalleeModel->results().resultData;
        const auto callees = index.data(CallerCalleeModel::CalleesRole).value<CalleeMap>();
        calleesModel->setResults(callees, resultData);
        const auto callers = index.data(CallerCalleeModel::CallersRole).value<CallerMap>();
        callersModel->setResults(callers, resultData);
        const auto sourceMap = index.data(CallerCalleeModel::SourceMapRole).value<LocationCostMap>();
        sourceMapModel->setResults(sourceMap, resultData);
        if (index.model() != m_ui->callerCalleeResults->model()) {
            m_ui->callerCalleeResults->setCurrentIndex(
                qobject_cast<QSortFilterProxyModel*>(m_ui->callerCalleeResults->model())->mapFromSource(index));
        }
    };
    auto showSymbolInCallerCallee = [this, callerCalleeModel, selectCallerCaleeeIndex](const Symbol& symbol) {
        m_ui->tabWidget->setCurrentWidget(m_ui->callerCalleeTab);
        selectCallerCaleeeIndex(callerCalleeModel->indexForSymbol(symbol));
    };
    connect(m_ui->flameGraphTab, &FlameGraph::callerCalleeViewRequested, this, showSymbolInCallerCallee);

    auto costDelegate = new CostDelegate(TreeModel::SortRole, TreeModel::MaxCostRole, this);
    setupTreeModel(bottomUpModel, m_ui->bottomUpResults, costDelegate, m_ui->bottomUpFilterFunction,
                   m_ui->bottomUpFilterModule, showSymbolInCallerCallee);

    setupTreeModel(topDownModel, m_ui->topDownResults, costDelegate, m_ui->topDownFilterFunction,
                   m_ui->topDownFilterModule, showSymbolInCallerCallee);

    setupCallerCallee(callerCalleeModel, m_ui->callerCalleeResults, m_ui->callerCalleeFilterFunction,
                      m_ui->callerCalleeFilterModule);

    connectCallerOrCalleeModel<CalleeModel>(m_ui->calleeView, callerCalleeModel, selectCallerCaleeeIndex);
    connectCallerOrCalleeModel<CallerModel>(m_ui->callerView, callerCalleeModel, selectCallerCaleeeIndex);
    addLocationContextMenu(m_ui->locationView, this);

    connect(m_ui->callerCalleeResults->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [selectCallerCaleeeIndex](const QModelIndex& current, const QModelIndex&) {
                if (current.isValid()) {
                    selectCallerCaleeeIndex(current);
                }
            });

    auto validateInputFile = [this](const QString& path, bool allowEmpty) -> bool {
        if (path.isEmpty()) {
            return allowEmpty;
        }

        const auto file = QFileInfo(path);
        if (!file.exists()) {
            showError(i18n("Input data %1 does not exist.", path));
        } else if (!file.isFile()) {
            showError(i18n("Input data %1 is not a file.", path));
        } else if (!file.isReadable()) {
            showError(i18n("Input data %1 is not readable.", path));
        } else {
            return true;
        }
        return false;
    };

    auto validateInput = [this, validateInputFile]() {
        m_ui->messages->hide();
        m_ui->buttonBox->setEnabled(validateInputFile(m_ui->openFile->url().toLocalFile(), false)
                                    && validateInputFile(m_ui->compareTo->url().toLocalFile(), true)
                                    && validateInputFile(m_ui->suppressions->url().toLocalFile(), true));
    };

    connect(m_ui->openFile, &KUrlRequester::textChanged, this, validateInput);
    connect(m_ui->compareTo, &KUrlRequester::textChanged, this, validateInput);
    connect(m_ui->suppressions, &KUrlRequester::textChanged, this, validateInput);
    connect(m_ui->buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        const auto path = m_ui->openFile->url().toLocalFile();
        Q_ASSERT(!path.isEmpty());
        const auto base = m_ui->compareTo->url().toLocalFile();

        bool parsedOk = false;
        m_lastFilterParameters.suppressions =
            parseSuppressions(m_ui->suppressions->url().toLocalFile().toStdString(), &parsedOk);
        if (parsedOk) {
            loadFile(path, base);
        } else {
            showError(i18n("Failed to parse suppression file."));
        }
    });

    setupStacks();

    setupTopView(bottomUpModel, m_ui->topPeak, TopProxy::Peak, showSymbolInCallerCallee);
    m_ui->topPeak->setItemDelegate(costDelegate);
    setupTopView(bottomUpModel, m_ui->topLeaked, TopProxy::Leaked, showSymbolInCallerCallee);
    m_ui->topLeaked->setItemDelegate(costDelegate);
    setupTopView(bottomUpModel, m_ui->topAllocations, TopProxy::Allocations, showSymbolInCallerCallee);
    m_ui->topAllocations->setItemDelegate(costDelegate);
    setupTopView(bottomUpModel, m_ui->topTemporary, TopProxy::Temporary, showSymbolInCallerCallee);
    m_ui->topTemporary->setItemDelegate(costDelegate);

    setWindowTitle(i18n("Heaptrack"));
    // closing the current file shows the stack page to open a new one
    m_openAction = KStandardAction::open(this, SLOT(closeFile()), this);
    m_openAction->setEnabled(false);
    m_ui->menu_File->addAction(m_openAction);
    m_openNewAction = KStandardAction::openNew(this, SLOT(openNewFile()), this);
    m_ui->menu_File->addAction(m_openNewAction);
    m_closeAction = KStandardAction::close(this, SLOT(close()), this);
    m_ui->menu_File->addAction(m_closeAction);
    m_quitAction = KStandardAction::quit(qApp, SLOT(quit()), this);
    m_ui->menu_File->addAction(m_quitAction);
    QShortcut* shortcut = new QShortcut(QKeySequence(QKeySequence::Copy), m_ui->stacksTree);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        QTreeView* view = m_ui->stacksTree;
        if (view->selectionModel()->hasSelection()) {
            QString text;
            const auto range = view->selectionModel()->selection().first();
            for (auto i = range.top(); i <= range.bottom(); ++i) {
                QStringList rowContents;
                for (auto j = range.left(); j <= range.right(); ++j)
                    rowContents << view->model()->index(i, j).data().toString();
                text += rowContents.join(QLatin1Char('\t'));
                text += QLatin1Char('\n');
            }
            QApplication::clipboard()->setText(text);
        }
    });

    m_disableEmbeddedSuppressions = m_ui->menu_Settings->addAction(i18n("Disable Embedded Suppressions"));
    m_disableEmbeddedSuppressions->setToolTip(
        i18n("Ignore suppression definitions that are embedded into the heaptrack data file. By default, heaptrack "
             "will copy the suppressions optionally defined via a `const char *__lsan_default_suppressions()` symbol "
             "in the debuggee application.  These are then always applied when analyzing the data, unless this feature "
             "is explicitly disabled using this command line option."));
    m_disableEmbeddedSuppressions->setCheckable(true);
    connect(m_disableEmbeddedSuppressions, &QAction::toggled, this, [this]() {
        m_lastFilterParameters.disableEmbeddedSuppressions = m_disableEmbeddedSuppressions->isChecked();
        reparse(m_lastFilterParameters.minTime, m_lastFilterParameters.maxTime);
    });

    m_disableBuiltinSuppressions = m_ui->menu_Settings->addAction(i18n("Disable Builtin Suppressions"));
    m_disableBuiltinSuppressions->setToolTip(i18n(
        "Ignore suppression definitions that are built into heaptrack. By default, heaptrack will suppress certain "
        "known leaks from common system libraries."));
    m_disableBuiltinSuppressions->setCheckable(true);
    connect(m_disableBuiltinSuppressions, &QAction::toggled, this, [this]() {
        m_lastFilterParameters.disableBuiltinSuppressions = m_disableBuiltinSuppressions->isChecked();
        reparse(m_lastFilterParameters.minTime, m_lastFilterParameters.maxTime);
    });

    setupCodeNavigationMenu();

    m_ui->actionResetFilter->setEnabled(false);
    connect(m_ui->actionResetFilter, &QAction::triggered, this,
            [this]() { reparse(0, std::numeric_limits<int64_t>::max()); });
    QObject::connect(m_parser, &Parser::finished, this,
                     [this]() { m_ui->actionResetFilter->setEnabled(m_parser->isFiltered()); });
}

MainWindow::~MainWindow()
{
    auto state = saveState(MAINWINDOW_VERSION);
    auto group = m_config->group(Config::Groups::MainWindow);
    group.writeEntry(Config::Entries::State, state);
}

void MainWindow::loadFile(const QString& file, const QString& diffBase)
{
    // TODO: support canceling of ongoing parse jobs
    m_closeAction->setEnabled(false);
    m_ui->loadingLabel->setText(i18n("Loading file %1, please wait...", file));
    if (diffBase.isEmpty()) {
        setWindowTitle(i18nc("%1: file name that is open", "Heaptrack - %1", QFileInfo(file).fileName()));
        m_diffMode = false;
    } else {
        setWindowTitle(i18nc("%1, %2: file names that are open", "Heaptrack - %1 compared to %2",
                             QFileInfo(file).fileName(), QFileInfo(diffBase).fileName()));
        m_diffMode = true;
    }
    m_ui->pages->setCurrentWidget(m_ui->loadingPage);
    m_parser->parse(file, diffBase, m_lastFilterParameters);
}

void MainWindow::reparse(int64_t minTime, int64_t maxTime)
{
    if (m_ui->pages->currentWidget() != m_ui->resultsPage) {
        return;
    }

    m_closeAction->setEnabled(false);
    m_ui->flameGraphTab->clearData();
    m_ui->loadingLabel->setText(i18n("Reparsing file, please wait..."));
    m_ui->pages->setCurrentWidget(m_ui->loadingPage);
    m_lastFilterParameters.minTime = minTime;
    m_lastFilterParameters.maxTime = maxTime;
    m_parser->reparse(m_lastFilterParameters);
}

void MainWindow::openNewFile()
{
    auto window = new MainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose, true);
    window->show();
    window->setDisableEmbeddedSuppressions(m_lastFilterParameters.disableEmbeddedSuppressions);
    window->setSuppressions(m_lastFilterParameters.suppressions);
}

void MainWindow::closeFile()
{
    m_ui->pages->setCurrentWidget(m_ui->openPage);

    m_ui->tabWidget->setCurrentIndex(m_ui->tabWidget->indexOf(m_ui->summaryTab));
    for (int i = 0, c = m_ui->tabWidget->count(); i < c; ++i) {
        m_ui->tabWidget->setTabEnabled(i, false);
    }

    m_openAction->setEnabled(false);
    emit clearData();
}

void MainWindow::showError(const QString& message)
{
    m_ui->messages->setText(message);
    m_ui->messages->show();
}

void MainWindow::setupStacks()
{
    auto stacksModel = new StacksModel(this);
    m_ui->stacksTree->setModel(stacksModel);
    m_ui->stacksTree->setRootIsDecorated(false);

    auto updateStackSpinner = [this](int stacks) {
        m_ui->stackSpinner->setMinimum(min(stacks, 1));
        m_ui->stackSpinner->setSuffix(i18n(" / %1", stacks));
        m_ui->stackSpinner->setMaximum(stacks);
    };
    updateStackSpinner(0);
    connect(stacksModel, &StacksModel::stacksFound, this, updateStackSpinner);
    connect(m_ui->stackSpinner, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), stacksModel,
            &StacksModel::setStackIndex);

    auto fillFromIndex = [stacksModel](const QModelIndex& current) {
        if (!current.isValid()) {
            stacksModel->clear();
        } else {
            auto proxy = qobject_cast<const TreeProxy*>(current.model());
            Q_ASSERT(proxy);
            auto leaf = proxy->mapToSource(current);
            stacksModel->fillFromIndex(leaf);
        }
    };
    connect(m_ui->bottomUpResults->selectionModel(), &QItemSelectionModel::currentChanged, this, fillFromIndex);
    connect(m_ui->topDownResults->selectionModel(), &QItemSelectionModel::currentChanged, this, fillFromIndex);

    auto tabChanged = [this, fillFromIndex](int tabIndex) {
        const auto widget = m_ui->tabWidget->widget(tabIndex);
        const bool showDocks = (widget == m_ui->topDownTab || widget == m_ui->bottomUpTab);
        m_ui->stacksDock->setVisible(showDocks);
        if (showDocks) {
            auto tree = (widget == m_ui->topDownTab) ? m_ui->topDownResults : m_ui->bottomUpResults;
            fillFromIndex(tree->selectionModel()->currentIndex());
        }
    };
    connect(m_ui->tabWidget, &QTabWidget::currentChanged, this, tabChanged);
    connect(m_parser, &Parser::bottomUpDataAvailable, this, [tabChanged]() { tabChanged(0); });

    m_ui->stacksDock->setVisible(false);
}


void MainWindow::setupCodeNavigationMenu()
{
    // Code Navigation
    QAction* configAction =
        new QAction(QIcon::fromTheme(QStringLiteral("applications-development")), i18n("Code Navigation"), this);
    auto menu = new QMenu(this);
    auto group = new QActionGroup(this);
    group->setExclusive(true);

    const auto settings = m_config->group(Config::Groups::CodeNavigation);
    const auto currentIdx = settings.readEntry(Config::Entries::IDE, firstAvailableIde());

    for (int i = 0; i < ideSettingsSize; ++i) {
        auto action = new QAction(menu);
        action->setText(ideSettings[i].name.toString());
        auto icon = QIcon::fromTheme(QString::fromUtf8(ideSettings[i].app));
        if (icon.isNull()) {
            icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
        }
        action->setIcon(icon);
        action->setCheckable(true);
        action->setChecked(currentIdx == i);
        action->setData(i);
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0) // It's not worth it to reimplement missing findExecutable for Qt4.
        action->setEnabled(isAppAvailable(ideSettings[i].app));
#endif
        group->addAction(action);
        menu->addAction(action);
    }
    menu->addSeparator();

    QAction* action = new QAction(menu);
    action->setText(i18n("Custom..."));
    action->setCheckable(true);
    action->setChecked(currentIdx == -1);
    action->setData(-1);
    action->setIcon(QIcon::fromTheme(QStringLiteral("application-x-executable-script")));
    group->addAction(action);
    menu->addAction(action);

#if defined(Q_OS_WIN) || defined(Q_OS_OSX)
    // This is a workaround for the cases, where we can't safely do assumptions
    // about the install location of the IDE
    action = new QAction(menu);
    action->setText(i18n("Automatic (No Line numbers)"));
    action->setCheckable(true);
    action->setChecked(currentIdx == -2);
    action->setData(-2);
    group->addAction(action);
    menu->addAction(action);
#endif

    QObject::connect(group, &QActionGroup::triggered, this, &MainWindow::setCodeNavigationIDE);

    configAction->setMenu(menu);
    m_ui->menu_Settings->addMenu(menu);
}

void MainWindow::setCodeNavigationIDE(QAction* action)
{
    auto settings = m_config->group(Config::Groups::CodeNavigation);

    if (action->data() == -1) {
        const auto customCmd =
            QInputDialog::getText(this, i18n("Custom Code Navigation"),
                                  i18n("Specify command to use for code navigation, '%f' will be replaced by the file "
                                     "name, '%l' by the line number and '%c' by the column number."),
                                  QLineEdit::Normal, settings.readEntry(Config::Entries::CustomCommand));
        if (!customCmd.isEmpty()) {
            settings.writeEntry(Config::Entries::CustomCommand, customCmd);
            settings.writeEntry(Config::Entries::IDE, -1);
        }
        return;
    }

    const auto defaultIde = action->data().toInt();
    settings.writeEntry(Config::Entries::IDE, defaultIde);
}

void MainWindow::navigateToCode(const QString& filePath, int lineNumber, int columnNumber)
{
    const auto settings = m_config->group(Config::Groups::CodeNavigation);
    const auto ideIdx = settings.readEntry(Config::Entries::IDE, firstAvailableIde());

    QString command;
    if (ideIdx >= 0 && ideIdx < ideSettingsSize) {
        command = QString::fromUtf8(ideSettings[ideIdx].app) + QLatin1Char(' ') + QString::fromUtf8(ideSettings[ideIdx].args);
    } else if (ideIdx == -1) {
        command = settings.readEntry(Config::Entries::CustomCommand);
    }

    if (!command.isEmpty()) {
        command.replace(QStringLiteral("%f"), filePath);
        command.replace(QStringLiteral("%l"), QString::number(std::max(1, lineNumber)));
        command.replace(QStringLiteral("%c"), QString::number(std::max(1, columnNumber)));

        auto splitted = KShell::splitArgs(command);
        QProcess::startDetached(splitted.takeFirst(), splitted);
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
    }
}

void MainWindow::setDisableEmbeddedSuppressions(bool disable)
{
    m_disableEmbeddedSuppressions->setChecked(disable);
}

void MainWindow::setDisableBuiltinSuppressions(bool disable)
{
    m_disableBuiltinSuppressions->setChecked(disable);
}

void MainWindow::setSuppressions(std::vector<std::string> suppressions)
{
    m_lastFilterParameters.suppressions = std::move(suppressions);
}
