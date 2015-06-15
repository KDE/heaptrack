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

#include "model.h"
#include "proxy.h"

using namespace std;

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_ui(new Ui::MainWindow)
    , m_model(new Model(this))
{
    m_ui->setupUi(this);
    auto proxy = new Proxy(m_model);
    proxy->setSourceModel(m_model);
    m_ui->results->setModel(proxy);

    m_ui->pages->setCurrentWidget(m_ui->openPage);
    // TODO: proper progress report
    m_ui->loadingProgress->setMinimum(0);
    m_ui->loadingProgress->setMaximum(0);

    connect(m_model, &Model::dataReady,
            this, &MainWindow::dataReady);

    m_ui->results->hideColumn(Model::FunctionColumn);
    m_ui->results->hideColumn(Model::FileColumn);
    m_ui->results->hideColumn(Model::ModuleColumn);

    connect(m_ui->filterFunction, &QLineEdit::textChanged,
            proxy, &Proxy::setFunctionFilter);
    connect(m_ui->filterFile, &QLineEdit::textChanged,
            proxy, &Proxy::setFileFilter);
    connect(m_ui->filterModule, &QLineEdit::textChanged,
            proxy, &Proxy::setModuleFilter);

    auto openFile = KStandardAction::open(this, SLOT(openFile()), this);
    m_ui->openFile->setDefaultAction(openFile);

    setWindowTitle(i18n("Heaptrack"));
}

MainWindow::~MainWindow()
{
}

void MainWindow::dataReady(const QString& summary)
{
    m_ui->summary->setText(summary);
    m_ui->pages->setCurrentWidget(m_ui->resultsPage);
}

void MainWindow::loadFile(const QString& file)
{
    m_ui->loadingLabel->setText(i18n("Loading file %1, please wait...", file));
    setWindowTitle(i18nc("%1: file name that is open", "Heaptrack - %1", file));
    m_ui->pages->setCurrentWidget(m_ui->loadingPage);
    m_model->loadFile(file);
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
