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

#include "treemodel.h"
#include "treeproxy.h"
#include "parser.h"
#include "chartmodel.h"
#include "chartproxy.h"
#include "histogrammodel.h"

using namespace std;

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_ui(new Ui::MainWindow)
    , m_bottomUpModel(new TreeModel(this))
    , m_topDownModel(new TreeModel(this))
    , m_parser(new Parser(this))
{
    m_ui->setupUi(this);

    m_ui->pages->setCurrentWidget(m_ui->openPage);
    // TODO: proper progress report
    m_ui->loadingProgress->setMinimum(0);
    m_ui->loadingProgress->setMaximum(0);

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

    connect(m_parser, &Parser::bottomUpDataAvailable,
            m_bottomUpModel, &TreeModel::resetData);
    connect(m_parser, &Parser::topDownDataAvailable,
            m_topDownModel, &TreeModel::resetData);
    connect(m_parser, &Parser::consumedChartDataAvailable,
            consumedModel, &ChartModel::resetData);
    connect(m_parser, &Parser::allocatedChartDataAvailable,
            allocatedModel, &ChartModel::resetData);
    connect(m_parser, &Parser::allocationsChartDataAvailable,
            allocationsModel, &ChartModel::resetData);
    connect(m_parser, &Parser::temporaryChartDataAvailable,
            temporaryModel, &ChartModel::resetData);
    connect(m_parser, &Parser::sizeHistogramDataAvailable,
            sizeHistogramModel, &HistogramModel::resetData);
    connect(m_parser, &Parser::summaryAvailable,
            m_ui->summary, &QLabel::setText);
    connect(m_parser, &Parser::topDownDataAvailable,
            m_ui->flameGraphTab, &FlameGraph::setTopDownData);
    connect(m_parser, &Parser::bottomUpDataAvailable,
            m_ui->flameGraphTab, &FlameGraph::setBottomUpData);
    connect(m_parser, &Parser::finished,
            this, [&] { m_ui->pages->setCurrentWidget(m_ui->resultsPage); });

    auto bottomUpProxy = new TreeProxy(m_bottomUpModel);
    bottomUpProxy->setSourceModel(m_bottomUpModel);
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

    auto topDownProxy = new TreeProxy(m_topDownModel);
    topDownProxy->setSourceModel(m_topDownModel);
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

    setWindowTitle(i18n("Heaptrack"));
}

MainWindow::~MainWindow()
{
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
