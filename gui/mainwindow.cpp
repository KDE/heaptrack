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

#include <iostream>

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

    connect(m_model, &Model::dataReady,
            this, &MainWindow::dataReady);

    connect(m_ui->filter, &QLineEdit::textChanged,
            proxy, &QSortFilterProxyModel::setFilterFixedString);
}

MainWindow::~MainWindow()
{
}

void MainWindow::dataReady(const QString& summary)
{
    m_ui->summary->setText(summary);
}

void MainWindow::loadFile(const QString& file)
{
    cout << "Loading file " << qPrintable(file) << ", this might take some time - please wait." << endl;

    m_model->loadFile(file);
}
