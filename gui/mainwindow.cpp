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

#include "bottomupmodel.h"
#include "bottomupproxy.h"
#include "parser.h"
#include "chartmodel.h"
#include "chartproxy.h"

using namespace std;

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_ui(new Ui::MainWindow)
    , m_bottomUpModel(new BottomUpModel(this))
    , m_chartModel(new ChartModel(this))
    , m_parser(new Parser(this))
{
    m_ui->setupUi(this);

    m_ui->pages->setCurrentWidget(m_ui->openPage);
    // TODO: proper progress report
    m_ui->loadingProgress->setMinimum(0);
    m_ui->loadingProgress->setMaximum(0);

    auto leakedProxy = new ChartProxy(i18n("Memory Leaked"), 1, this);
    leakedProxy->setSourceModel(m_chartModel);
    m_ui->leakedTab->setModel(leakedProxy);

    auto allocationsProxy = new ChartProxy(i18n("Memory Allocations"), 2, this);
    allocationsProxy->setSourceModel(m_chartModel);
    m_ui->allocationsTab->setModel(allocationsProxy);

    auto allocatedProxy = new ChartProxy(i18n("Memory Allocated"), 3, this);
    allocatedProxy->setSourceModel(m_chartModel);
    m_ui->allocatedTab->setModel(allocatedProxy);

    connect(m_parser, &Parser::bottomUpDataAvailable,
            m_bottomUpModel, &BottomUpModel::resetData);
    connect(m_parser, &Parser::chartDataAvailable,
            m_chartModel, &ChartModel::resetData);
    connect(m_parser, &Parser::summaryAvailable,
            m_ui->summary, &QLabel::setText);
    connect(m_parser, &Parser::finished,
            this, [&] { m_ui->pages->setCurrentWidget(m_ui->resultsPage); });

    auto bottomUpProxy = new BottomUpProxy(m_bottomUpModel);
    bottomUpProxy->setSourceModel(m_bottomUpModel);
    m_ui->results->setModel(bottomUpProxy);
    m_ui->results->hideColumn(BottomUpModel::FunctionColumn);
    m_ui->results->hideColumn(BottomUpModel::FileColumn);
    m_ui->results->hideColumn(BottomUpModel::LineColumn);
    m_ui->results->hideColumn(BottomUpModel::ModuleColumn);

    connect(m_ui->filterFunction, &QLineEdit::textChanged,
            bottomUpProxy, &BottomUpProxy::setFunctionFilter);
    connect(m_ui->filterFile, &QLineEdit::textChanged,
            bottomUpProxy, &BottomUpProxy::setFileFilter);
    connect(m_ui->filterModule, &QLineEdit::textChanged,
            bottomUpProxy, &BottomUpProxy::setModuleFilter);

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
