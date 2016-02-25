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

#include "mainwindow.h"

#include <ui_mainwindow.h>

#include <KRecursiveFilterProxyModel>
#include <KStandardAction>
#include <KLocalizedString>

#include <QFileDialog>
#include <QStatusBar>
#include <KConfigGroup>
#include <QDebug>

#include "treemodel.h"
#include "treeproxy.h"
#include "topproxy.h"
#include "parser.h"
#include "chartmodel.h"
#include "chartproxy.h"
#include "histogrammodel.h"
#include "stacksmodel.h"

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

    auto consumedModel = new ChartModel(ChartModel::Consumed, this);
    m_ui->consumedTab->setModel(consumedModel);
    auto allocationsModel = new ChartModel(ChartModel::Allocations, this);
    m_ui->allocationsTab->setModel(allocationsModel);
    auto allocatedModel = new ChartModel(ChartModel::Allocated, this);
    m_ui->allocatedTab->setModel(allocatedModel);
    auto temporaryModel = new ChartModel(ChartModel::Temporary, this);
    m_ui->temporaryTab->setModel(temporaryModel);
    auto sizeHistogramModel = new HistogramModel(this);
    m_ui->sizesTab->setModel(sizeHistogramModel);

    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->consumedTab), false);
    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->allocationsTab), false);
    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->allocatedTab), false);
    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->temporaryTab), false);
    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->sizesTab), false);
    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->topDownTab), false);
    m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->flameGraphTab), false);

    connect(m_parser, &Parser::bottomUpDataAvailable,
            this, [=] (const TreeData& data) {
        bottomUpModel->resetData(data);
        m_ui->flameGraphTab->setBottomUpData(data);
        m_ui->progressLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
        statusBar()->addWidget(m_ui->progressLabel, 1);
        statusBar()->addWidget(m_ui->loadingProgress);
        m_ui->pages->setCurrentWidget(m_ui->resultsPage);
    });
    connect(m_parser, &Parser::topDownDataAvailable,
            this, [=] (const TreeData& data) {
                topDownModel->resetData(data);
                m_ui->flameGraphTab->setTopDownData(data);
                m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->topDownTab), true);
                m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->flameGraphTab), true);
            });
    connect(m_parser, &Parser::consumedChartDataAvailable,
            this, [=] (const ChartData& data) {
                consumedModel->resetData(data);
                m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->consumedTab), true);
            });
    connect(m_parser, &Parser::allocatedChartDataAvailable,
            this, [=] (const ChartData& data) {
                allocatedModel->resetData(data);
                m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->allocatedTab), true);
            });
    connect(m_parser, &Parser::allocationsChartDataAvailable,
            this, [=] (const ChartData& data) {
                allocationsModel->resetData(data);
                m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->allocationsTab), true);
            });
    connect(m_parser, &Parser::temporaryChartDataAvailable,
            this, [=] (const ChartData& data) {
                temporaryModel->resetData(data);
                m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->temporaryTab), true);
            });
    connect(m_parser, &Parser::sizeHistogramDataAvailable,
            this, [=] (const HistogramData& data) {
                sizeHistogramModel->resetData(data);
                m_ui->tabWidget->setTabEnabled(m_ui->tabWidget->indexOf(m_ui->sizesTab), true);
            });
    connect(m_parser, &Parser::summaryAvailable,
            this, [this] (const SummaryData& data) {
                KFormat format;
                QString textLeft;
                QString textCenter;
                QString textRight;
                const double totalTimeS = 0.001 * data.totalTime;
                const double peakTimeS = 0.001 * data.peakTime;
                {
                    QTextStream stream(&textLeft);
                    stream << "<qt><dl>"
                           << i18n("<dt><b>debuggee</b>:</dt><dd style='font-family:monospace;'>%1</dd>", data.debuggee)
                           // xgettext:no-c-format
                           << i18n("<dt><b>total runtime</b>:</dt><dd>%1s</dd>", totalTimeS)
                           << i18n("<dt><b>total system memory</b>:</dt><dd>%1s</dd>", format.formatByteSize(data.totalSystemMemory))
                           << "</dl></qt>";
                }
                {
                    QTextStream stream(&textCenter);
                    stream << "<qt><dl>"
                           << i18n("<dt><b>calls to allocation functions</b>:</dt><dd>%1 (%2/s)</dd>",
                                   data.allocations, quint64(data.allocations / totalTimeS))
                           << i18n("<dt><b>temporary allocations</b>:</dt><dd>%1 (%2%, %3/s)</dd>",
                                   data.temporary, round(float(data.temporary) * 100.f * 100.f / data.allocations) / 100.f,
                                   quint64(data.temporary / totalTimeS))
                           << i18n("<dt><b>bytes allocated in total</b> (ignoring deallocations):</dt><dd>%1 (%2/s)</dd>",
                                   format.formatByteSize(data.allocated, 2), format.formatByteSize(data.allocated / totalTimeS))
                           << "</dl></qt>";
                }
                {
                    QTextStream stream(&textRight);
                    stream << "<qt><dl>"
                           << i18n("<dt><b>peak heap memory consumption</b>:</dt><dd>%1 after %2s</dd>", format.formatByteSize(data.peak), peakTimeS)
                           << i18n("<dt><b>peak RSS</b> (including heaptrack overhead):</dt><dd>%1</dd>", format.formatByteSize(data.peakRSS))
                           << i18n("<dt><b>total memory leaked</b>:</dt><dd>%1</dd>", format.formatByteSize(data.leaked))
                           << "</dl></qt>";
                }

                m_ui->summaryLeft->setText(textLeft);
                m_ui->summaryCenter->setText(textCenter);
                m_ui->summaryRight->setText(textRight);
            });
    connect(m_parser, &Parser::progressMessageAvailable,
            m_ui->progressLabel, &QLabel::setText);
    auto removeProgress = [this] {
        statusBar()->removeWidget(m_ui->progressLabel);
        statusBar()->removeWidget(m_ui->loadingProgress);
    };
    connect(m_parser, &Parser::finished,
            this, removeProgress);
    connect(m_parser, &Parser::failedToOpen,
            this, [this, removeProgress] (const QString& failedFile) {
        removeProgress();
        m_ui->pages->setCurrentWidget(m_ui->openPage);
        m_ui->messages->setText(i18n("Failed to parse file %1.", failedFile));
        m_ui->messages->show();
    });
    m_ui->messages->hide();

    auto bottomUpProxy = new TreeProxy(bottomUpModel);
    bottomUpProxy->setSourceModel(bottomUpModel);
    bottomUpProxy->setSortRole(TreeModel::SortRole);
    m_ui->bottomUpResults->setModel(bottomUpProxy);
    m_ui->bottomUpResults->hideColumn(TreeModel::FunctionColumn);
    m_ui->bottomUpResults->hideColumn(TreeModel::FileColumn);
    m_ui->bottomUpResults->hideColumn(TreeModel::LineColumn);
    m_ui->bottomUpResults->hideColumn(TreeModel::ModuleColumn);
    connect(m_ui->bottomUpFilterFunction, &QLineEdit::textChanged,
            bottomUpProxy, &TreeProxy::setFunctionFilter);
    connect(m_ui->bottomUpFilterFile, &QLineEdit::textChanged,
            bottomUpProxy, &TreeProxy::setFileFilter);
    connect(m_ui->bottomUpFilterModule, &QLineEdit::textChanged,
            bottomUpProxy, &TreeProxy::setModuleFilter);

    auto topDownProxy = new TreeProxy(topDownModel);
    topDownProxy->setSourceModel(topDownModel);
    topDownProxy->setSortRole(TreeModel::SortRole);
    m_ui->topDownResults->setModel(topDownProxy);
    m_ui->topDownResults->hideColumn(TreeModel::FunctionColumn);
    m_ui->topDownResults->hideColumn(TreeModel::FileColumn);
    m_ui->topDownResults->hideColumn(TreeModel::LineColumn);
    m_ui->topDownResults->hideColumn(TreeModel::ModuleColumn);
    connect(m_ui->topDownFilterFunction, &QLineEdit::textChanged,
            bottomUpProxy, &TreeProxy::setFunctionFilter);
    connect(m_ui->topDownFilterFile, &QLineEdit::textChanged,
            bottomUpProxy, &TreeProxy::setFileFilter);
    connect(m_ui->topDownFilterModule, &QLineEdit::textChanged,
            bottomUpProxy, &TreeProxy::setModuleFilter);

    auto openFile = KStandardAction::open(this, SLOT(openFile()), this);
    m_ui->openFile->setDefaultAction(openFile);

    setupStacks();

    setupTopView(bottomUpModel, m_ui->topPeak, TopProxy::Peak);
    setupTopView(bottomUpModel, m_ui->topLeaked, TopProxy::Leaked);
    setupTopView(bottomUpModel, m_ui->topAllocations, TopProxy::Allocations);
    setupTopView(bottomUpModel, m_ui->topTemporary, TopProxy::Temporary);
    setupTopView(bottomUpModel, m_ui->topAllocated, TopProxy::Allocated);

    setWindowTitle(i18n("Heaptrack"));
}

MainWindow::~MainWindow()
{
    auto state = saveState(MAINWINDOW_VERSION);
    auto group = m_config->group(Config::Groups::MainWindow);
    group.writeEntry(Config::Entries::State, state);
}

void MainWindow::loadFile(const QString& file)
{
    m_ui->loadingLabel->setText(i18n("Loading file %1, please wait...", file));
    setWindowTitle(i18nc("%1: file name that is open", "Heaptrack - %1", file));
    m_ui->pages->setCurrentWidget(m_ui->loadingPage);
    m_parser->parse(file);
}

void MainWindow::openFile()
{
    auto dialog = new QFileDialog(this, i18n("Open Heaptrack Output File"), {}, i18n("Heaptrack data files (heaptrack.*)"));
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setFileMode(QFileDialog::ExistingFile);
    connect(dialog, &QFileDialog::fileSelected,
            this, &MainWindow::loadFile);
    dialog->show();
}

void MainWindow::setupStacks()
{
    auto stacksModel = new StacksModel(this);
    m_ui->stacksTree->setModel(stacksModel);
    m_ui->stacksTree->setRootIsDecorated(false);

    auto updateStackSpinner = [this] (int stacks) {
        m_ui->stackSpinner->setMinimum(min(stacks, 1));
        m_ui->stackSpinner->setSuffix(i18n(" / %1", stacks));
        m_ui->stackSpinner->setMaximum(stacks);
    };
    updateStackSpinner(0);
    connect(stacksModel, &StacksModel::stacksFound,
            this, updateStackSpinner);
    connect(m_ui->stackSpinner, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            stacksModel, &StacksModel::setStackIndex);

    auto fillFromIndex = [stacksModel] (const QModelIndex& current) {
        if (!current.isValid()) {
            stacksModel->clear();
        } else {
            auto proxy = qobject_cast<const TreeProxy*>(current.model());
            Q_ASSERT(proxy);
            auto leaf = proxy->mapToSource(current);
            stacksModel->fillFromIndex(leaf);
        }
    };
    connect(m_ui->bottomUpResults->selectionModel(), &QItemSelectionModel::currentChanged,
            this, fillFromIndex);
    connect(m_ui->topDownResults->selectionModel(), &QItemSelectionModel::currentChanged,
            this, fillFromIndex);

    auto tabChanged = [this, fillFromIndex] (int tabIndex) {
        const auto widget = m_ui->tabWidget->widget(tabIndex);
        const bool showDocks = (widget == m_ui->topDownTab || widget == m_ui->bottomUpTab);
        m_ui->stacksDock->setVisible(showDocks);
        if (showDocks) {
            auto tree = (widget == m_ui->topDownTab) ? m_ui->topDownResults : m_ui->bottomUpResults;
            fillFromIndex(tree->selectionModel()->currentIndex());
        }
    };
    connect(m_ui->tabWidget, &QTabWidget::currentChanged,
            this, tabChanged);
    connect(m_parser, &Parser::bottomUpDataAvailable,
            this, [tabChanged] () { tabChanged(0); });

    m_ui->stacksDock->setVisible(false);
    m_ui->stacksDock->setFeatures(QDockWidget::DockWidgetMovable);
}
