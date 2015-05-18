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

#include <iostream>
#include <sstream>

#include "model.h"
#include "../accumulatedtracedata.h"

using namespace std;

namespace {
QString generateSummary(const AccumulatedTraceData& data)
{
    stringstream stream;
    const double totalTimeS = 0.001 * data.totalTime;
    stream << "<qt>"
           << "<strong>total runtime</strong>: " << fixed << totalTimeS << "s.<br/>"
           << "<strong>bytes allocated in total</strong> (ignoring deallocations): " << formatBytes(data.totalAllocated)
             << " (" << formatBytes(data.totalAllocated / totalTimeS) << "/s)<br/>"
           << "<strong>calls to allocation functions</strong>: " << data.totalAllocations
             << " (" << size_t(data.totalAllocations / totalTimeS) << "/s)<br/>"
           << "<strong>peak heap memory consumption</strong>: " << formatBytes(data.peak) << "<br/>"
           << "<strong>total memory leaked</strong>: " << formatBytes(data.leaked) << "<br/>";
    stream << "</qt>";
    return QString::fromStdString(stream.str());
}
}

MainWindow::MainWindow(QWidget* parent)
    : m_ui(new Ui::MainWindow)
    , m_model(new Model(this))
{
    m_ui->setupUi(this);
    m_ui->results->setModel(m_model);
}

MainWindow::~MainWindow()
{
}

void MainWindow::loadFile(const QString& file)
{
    cout << "Loading file " << qPrintable(file) << ", this might take some time - please wait." << endl;
    AccumulatedTraceData data;
    data.read(file.toStdString());

    m_ui->summary->setText(generateSummary(data));
}
