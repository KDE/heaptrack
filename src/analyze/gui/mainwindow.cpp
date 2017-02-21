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

#include "mainwindow.h"

#include <ui_mainwindow.h>

#include <cmath>

#include <KConfigGroup>
#include <KLocalizedString>
#include <KRecursiveFilterProxyModel>
#include <KStandardAction>

#include <QAction>
#include <QDebug>
#include <QDesktopServices>
#include <QFileDialog>
#include <QMenu>
#include <QStatusBar>

#include "callercalleemodel.h"
#include "costdelegate.h"
#include "parser.h"
#include "stacksmodel.h"
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
}
namespace Entries {
const char State[] = "State";
}
}

void addContextMenu(QTreeView* treeView, int role)
{
    treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(treeView, &QTreeView::customContextMenuRequested, treeView, [treeView, role](const QPoint& pos) {
        auto index = treeView->indexAt(pos);
        if (!index.isValid()) {
            return;
        }
        const auto location = index.data(role).value<LocationData::Ptr>();
        if (!location || !QFile::exists(location->file)) {
            return;
        }
        auto menu = new QMenu(treeView);
        auto openFile =
            new QAction(QIcon::fromTheme(QStringLiteral("document-open")), i18n("Open file in editor"), menu);
        QObject::connect(openFile, &QAction::triggered, openFile, [location] {
            /// FIXME: add settings to let user configure this
            auto url = QUrl::fromLocalFile(location->file);
            url.setFragment(QString::number(location->line));
            QDesktopServices::openUrl(url);
        });
        menu->addAction(openFile);
        menu->popup(treeView->mapToGlobal(pos));
    });
}

void setupTopView(TreeModel* source, QTreeView* view, TopProxy::Type type)
{
    auto proxy = new TopProxy(type, source);
    proxy->setSourceModel(source);
    proxy->setSortRole(TreeModel::SortRole);
    view->setModel(proxy);
    view->setRootIsDecorated(false);
    view->setUniformRowHeights(true);
    view->sortByColumn(0);
    view->header()->setStretchLastSection(true);
    addContextMenu(view, TreeModel::LocationRole);
}

#if KChart_FOUND
void addChartTab(QTabWidget* tabWidget, const QString& title, ChartModel::Type type, const Parser* parser,
                 void (Parser::*dataReady)(const ChartData&), MainWindow* window)
{
    auto tab = new ChartWidget(tabWidget->parentWidget());
    tabWidget->addTab(tab, title);
    tabWidget->setTabEnabled(tabWidget->indexOf(tab), false);
    auto model = new ChartModel(type, tab);
    tab->setModel(model);
    QObject::connect(parser, dataReady, tab, [=](const ChartData& data) {
        model->resetData(data);
        tabWidget->setTabEnabled(tabWidget->indexOf(tab), true);
    });
    QObject::connect(window, &MainWindow::clearData, model, &ChartModel::clearData);
}
#endif

void setupTreeModel(TreeModel* model, QTreeView* view, CostDelegate* costDelegate, QLineEdit* filterFunction,
                    QLineEdit* filterFile, QLineEdit* filterModule)
{
    auto proxy = new TreeProxy(TreeModel::FunctionColumn, TreeModel::FileColumn, TreeModel::ModuleColumn, model);
    proxy->setSourceModel(model);
    proxy->setSortRole(TreeModel::SortRole);

    view->setModel(proxy);
    view->setItemDelegateForColumn(TreeModel::PeakColumn, costDelegate);
    view->setItemDelegateForColumn(TreeModel::AllocatedColumn, costDelegate);
    view->setItemDelegateForColumn(TreeModel::LeakedColumn, costDelegate);
    view->setItemDelegateForColumn(TreeModel::AllocationsColumn, costDelegate);
    view->setItemDelegateForColumn(TreeModel::TemporaryColumn, costDelegate);
    view->hideColumn(TreeModel::FunctionColumn);
    view->hideColumn(TreeModel::FileColumn);
    view->hideColumn(TreeModel::LineColumn);
    view->hideColumn(TreeModel::ModuleColumn);

    QObject::connect(filterFunction, &QLineEdit::textChanged, proxy, &TreeProxy::setFunctionFilter);
    QObject::connect(filterFile, &QLineEdit::textChanged, proxy, &TreeProxy::setFileFilter);
    QObject::connect(filterModule, &QLineEdit::textChanged, proxy, &TreeProxy::setModuleFilter);
    addContextMenu(view, TreeModel::LocationRole);
}

void setupCallerCalle(CallerCalleeModel* model, QTreeView* view, CostDelegate* costDelegate, QLineEdit* filterFunction,
                      QLineEdit* filterFile, QLineEdit* filterModule)
{
    auto callerCalleeProxy = new TreeProxy(CallerCalleeModel::FunctionColumn, CallerCalleeModel::FileColumn,
                                           CallerCalleeModel::ModuleColumn, model);
    callerCalleeProxy->setSourceModel(model);
    callerCalleeProxy->setSortRole(CallerCalleeModel::SortRole);
    view->setModel(callerCalleeProxy);
    view->sortByColumn(CallerCalleeModel::InclusivePeakColumn);
    view->setItemDelegateForColumn(CallerCalleeModel::SelfPeakColumn, costDelegate);
    view->setItemDelegateForColumn(CallerCalleeModel::SelfAllocatedColumn, costDelegate);
    view->setItemDelegateForColumn(CallerCalleeModel::SelfLeakedColumn, costDelegate);
    view->setItemDelegateForColumn(CallerCalleeModel::SelfAllocationsColumn, costDelegate);
    view->setItemDelegateForColumn(CallerCalleeModel::SelfTemporaryColumn, costDelegate);
    view->setItemDelegateForColumn(CallerCalleeModel::InclusivePeakColumn, costDelegate);
    view->setItemDelegateForColumn(CallerCalleeModel::InclusiveAllocatedColumn, costDelegate);
    view->setItemDelegateForColumn(CallerCalleeModel::InclusiveLeakedColumn, costDelegate);
    view->setItemDelegateForColumn(CallerCalleeModel::InclusiveAllocationsColumn, costDelegate);
    view->setItemDelegateForColumn(CallerCalleeModel::InclusiveTemporaryColumn, costDelegate);
    view->hideColumn(CallerCalleeModel::FunctionColumn);
    view->hideColumn(CallerCalleeModel::FileColumn);
    view->hideColumn(CallerCalleeModel::LineColumn);
    view->hideColumn(CallerCalleeModel::ModuleColumn);
    QObject::connect(filterFunction, &QLineEdit::textChanged, callerCalleeProxy, &TreeProxy::setFunctionFilter);
    QObject::connect(filterFile, &QLineEdit::textChanged, callerCalleeProxy, &TreeProxy::setFileFilter);
    QObject::connect(filterModule, &QLineEdit::textChanged, callerCalleeProxy, &TreeProxy::setModuleFilter);
    addContextMenu(view, CallerCalleeModel::LocationRole);
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
    m_ui->loadingProgress->setMaximum(0);

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
    connect(m_parser, &Parser::callerCalleeDataAvailable, this, [=](const CallerCalleeRows& data) {
        callerCalleeModel->resetData(data);
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
        callerCalleeModel->setSummary(data);
        KFormat format;
        QString textLeft;
        QString textCenter;
        QString textRight;
        const double totalTimeS = 0.001 * data.totalTime;
        const double peakTimeS = 0.001 * data.peakTime;
        {
            QTextStream stream(&textLeft);
            const auto debuggee = insertWordWrapMarkers(data.debuggee);
            stream << "<qt><dl>"
                   << (data.fromAttached ? i18n("<dt><b>debuggee</b>:</dt><dd "
                                                "style='font-family:monospace;'>%1 <i>(attached)</i></dd>",
                                                debuggee)
                                         : i18n("<dt><b>debuggee</b>:</dt><dd "
                                                "style='font-family:monospace;'>%1</dd>",
                                                debuggee))
                   // xgettext:no-c-format
                   << i18n("<dt><b>total runtime</b>:</dt><dd>%1s</dd>", totalTimeS)
                   << i18n("<dt><b>total system memory</b>:</dt><dd>%1</dd>",
                           format.formatByteSize(data.totalSystemMemory))
                   << "</dl></qt>";
        }
        {
            QTextStream stream(&textCenter);
            stream << "<qt><dl>" << i18n("<dt><b>calls to allocation functions</b>:</dt><dd>%1 "
                                         "(%2/s)</dd>",
                                         data.cost.allocations, qint64(data.cost.allocations / totalTimeS))
                   << i18n("<dt><b>temporary allocations</b>:</dt><dd>%1 (%2%, "
                           "%3/s)</dd>",
                           data.cost.temporary,
                           std::round(float(data.cost.temporary) * 100.f * 100.f / data.cost.allocations) / 100.f,
                           qint64(data.cost.temporary / totalTimeS))
                   << i18n("<dt><b>bytes allocated in total</b> (ignoring "
                           "deallocations):</dt><dd>%1 (%2/s)</dd>",
                           format.formatByteSize(data.cost.allocated, 2),
                           format.formatByteSize(data.cost.allocated / totalTimeS))
                   << "</dl></qt>";
        }
        {
            QTextStream stream(&textRight);
            stream << "<qt><dl>" << i18n("<dt><b>peak heap memory consumption</b>:</dt><dd>%1 "
                                         "after %2s</dd>",
                                         format.formatByteSize(data.cost.peak), peakTimeS)
                   << i18n("<dt><b>peak RSS</b> (including heaptrack "
                           "overhead):</dt><dd>%1</dd>",
                           format.formatByteSize(data.peakRSS))
                   << i18n("<dt><b>total memory leaked</b>:</dt><dd>%1</dd>", format.formatByteSize(data.cost.leaked))
                   << "</dl></qt>";
        }

        m_ui->summaryLeft->setText(textLeft);
        m_ui->summaryCenter->setText(textCenter);
        m_ui->summaryRight->setText(textRight);
        m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->summaryTab), true);
    });
    connect(m_parser, &Parser::progressMessageAvailable, m_ui->progressLabel, &QLabel::setText);
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
    addChartTab(m_ui->tabWidget, i18n("Consumed"), ChartModel::Consumed, m_parser, &Parser::consumedChartDataAvailable,
                this);
    addChartTab(m_ui->tabWidget, i18n("Allocations"), ChartModel::Allocations, m_parser,
                &Parser::allocationsChartDataAvailable, this);
    addChartTab(m_ui->tabWidget, i18n("Temporary Allocations"), ChartModel::Temporary, m_parser,
                &Parser::temporaryChartDataAvailable, this);
    addChartTab(m_ui->tabWidget, i18n("Allocated"), ChartModel::Allocated, m_parser, &Parser::allocatedChartDataAvailable,
                this);

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

    auto costDelegate = new CostDelegate(this);

    setupTreeModel(bottomUpModel, m_ui->bottomUpResults, costDelegate, m_ui->bottomUpFilterFunction,
                   m_ui->bottomUpFilterFile, m_ui->bottomUpFilterModule);

    setupTreeModel(topDownModel, m_ui->topDownResults, costDelegate, m_ui->topDownFilterFunction,
                   m_ui->topDownFilterFile, m_ui->topDownFilterModule);

    setupCallerCalle(callerCalleeModel, m_ui->callerCalleeResults, costDelegate, m_ui->callerCalleeFilterFunction,
                     m_ui->callerCalleeFilterFile, m_ui->callerCalleeFilterModule);

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
                                    && validateInputFile(m_ui->compareTo->url().toLocalFile(), true));
    };

    connect(m_ui->openFile, &KUrlRequester::textChanged, this, validateInput);
    connect(m_ui->compareTo, &KUrlRequester::textChanged, this, validateInput);
    connect(m_ui->buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        const auto path = m_ui->openFile->url().toLocalFile();
        Q_ASSERT(!path.isEmpty());
        const auto base = m_ui->compareTo->url().toLocalFile();
        loadFile(path, base);
    });

    setupStacks();

    setupTopView(bottomUpModel, m_ui->topPeak, TopProxy::Peak);
    m_ui->topPeak->setItemDelegate(costDelegate);
    setupTopView(bottomUpModel, m_ui->topLeaked, TopProxy::Leaked);
    m_ui->topLeaked->setItemDelegate(costDelegate);
    setupTopView(bottomUpModel, m_ui->topAllocations, TopProxy::Allocations);
    m_ui->topAllocations->setItemDelegate(costDelegate);
    setupTopView(bottomUpModel, m_ui->topTemporary, TopProxy::Temporary);
    m_ui->topTemporary->setItemDelegate(costDelegate);
    setupTopView(bottomUpModel, m_ui->topAllocated, TopProxy::Allocated);
    m_ui->topAllocated->setItemDelegate(costDelegate);

    setWindowTitle(i18n("Heaptrack"));
    // closing the current file shows the stack page to open a new one
    m_openAction = KStandardAction::open(this, &MainWindow::closeFile, this);
    m_ui->menu_File->addAction(m_openAction);
    m_openNewAction = KStandardAction::openNew(this, &MainWindow::openNewFile, this);
    m_ui->menu_File->addAction(m_openNewAction);
    m_closeAction = KStandardAction::close(this, &MainWindow::close, this);
    m_closeAction->setEnabled(false); // TODO: support canceling of ongoing parse jobs
    m_ui->menu_File->addAction(m_closeAction);
    m_quitAction = KStandardAction::quit(qApp, &QCoreApplication::quit, this);
    m_ui->menu_File->addAction(m_quitAction);
}

MainWindow::~MainWindow()
{
    auto state = saveState(MAINWINDOW_VERSION);
    auto group = m_config->group(Config::Groups::MainWindow);
    group.writeEntry(Config::Entries::State, state);
}

void MainWindow::loadFile(const QString& file, const QString& diffBase)
{
    m_openAction->setEnabled(false);
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
    m_parser->parse(file, diffBase);
}

void MainWindow::openNewFile()
{
    auto window = new MainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose, true);
    window->show();
}

void MainWindow::closeFile()
{
    m_ui->pages->setCurrentWidget(m_ui->openPage);

    m_ui->tabWidget->setCurrentIndex(m_ui->tabWidget->indexOf(m_ui->summaryTab));
    for (int i = 0, c = m_ui->tabWidget->count(); i < c; ++i) {
        m_ui->tabWidget->setTabEnabled(i, false);
    }

    m_closeAction->setEnabled(false);
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
