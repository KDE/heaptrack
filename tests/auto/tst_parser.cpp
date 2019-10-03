/*
 * Copyright 2019 David Faure <david.faure@kdab.com>
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

#include "3rdparty/catch.hpp"

#include "parser.h"
#include "locationdata.h"
#include "treemodel.h"
#include "tst_config.h" // for SRC_DIR

#include <QDebug>
#include <QSignalSpy>

TEST_CASE ("parse sample file", "[parser]") {

    int argc = 0;
    QCoreApplication app(argc, nullptr);
    Parser parser;

    qRegisterMetaType<CallerCalleeResults>();
    QSignalSpy spyCCD(&parser, &Parser::callerCalleeDataAvailable);
    QSignalSpy spyBottomUp(&parser, &Parser::bottomUpDataAvailable);
    QSignalSpy spyTopDown(&parser, &Parser::topDownDataAvailable);

    parser.parse(SRC_DIR "/heaptrack.david.18594.gz", QString());

    // ---- Check Caller Callee Data

    if (spyCCD.isEmpty())
        REQUIRE(spyCCD.wait());

    const CallerCalleeResults ccr = spyCCD.at(0).at(0).value<CallerCalleeResults>();
    auto ccrSymbolList = ccr.entries.keys();
    std::sort(ccrSymbolList.begin(), ccrSymbolList.end(), Symbol::FullLessThan());
    if (!qgetenv("HEAPTRACK_DEBUG").isEmpty()) {
        for (const Symbol &sym : ccrSymbolList) {
            qDebug() << sym.symbol << sym.binary << sym.path;
        }
    }

    // Let's check a few items
    auto symbolToString = [](const Symbol &sym) { return sym.symbol + '|' + sym.binary + '|' + sym.path; };
    REQUIRE(symbolToString(ccrSymbolList.at(0)) == "<unresolved function>||");
    REQUIRE(symbolToString(ccrSymbolList.at(1)) == "<unresolved function>|ld-linux-x86-64.so.2|/lib64/ld-linux-x86-64.so.2");
    REQUIRE(symbolToString(ccrSymbolList.at(25)) == "QByteArray::constData() const|libQt5Core.so.5|/d/qt/5/kde/build/qtbase/lib/libQt5Core.so.5");
    const int lastIndx = ccrSymbolList.size() - 1;
    REQUIRE(symbolToString(ccrSymbolList.at(lastIndx)) == "~QVarLengthArray|libQt5Core.so.5|/d/qt/5/kde/build/qtbase/lib/libQt5Core.so.5");

    REQUIRE(ccr.entries.count() == 365);
    REQUIRE(ccr.totalCosts.allocations == 2896);

    // ---- Check Bottom Up Data

    if (spyBottomUp.isEmpty())
        REQUIRE(spyBottomUp.wait());

    const TreeData bottomUpData = spyBottomUp.at(0).at(0).value<TreeData>();
    if (!qgetenv("HEAPTRACK_DEBUG").isEmpty()) {
        qDebug() << "Bottom Up Data:";
        for (const RowData &row : bottomUpData) {
            qDebug() << symbolToString(row.symbol);
        }
    }
    REQUIRE(bottomUpData.size() == 54);
    REQUIRE(symbolToString(bottomUpData.at(0).symbol) == "<unresolved function>|libglib-2.0.so.0|/usr/lib64/libglib-2.0.so.0");
    REQUIRE(bottomUpData.at(0).children.size() == 2);
    REQUIRE(bottomUpData.at(0).cost.allocations == 17);
    REQUIRE(bottomUpData.at(0).cost.peak == 2020);
    REQUIRE(symbolToString(bottomUpData.at(53).symbol) == "QThreadPool::QThreadPool(QObject*)|libQt5Core.so.5|/d/qt/5/kde/build/qtbase/lib/libQt5Core.so.5");

    // ---- Check Top Down Data

    if (spyTopDown.isEmpty())
        REQUIRE(spyTopDown.wait());

    const TreeData topDownData = spyTopDown.at(0).at(0).value<TreeData>();
    if (!qgetenv("HEAPTRACK_DEBUG").isEmpty()) {
        qDebug() << "Top Down Data:";
        for (const RowData &row : topDownData) {
            qDebug() << symbolToString(row.symbol);
        }
    }
    REQUIRE(topDownData.size() == 5);
    REQUIRE(symbolToString(topDownData.at(0).symbol) == "<unresolved function>|ld-linux-x86-64.so.2|/lib64/ld-linux-x86-64.so.2");
    REQUIRE(topDownData.at(0).children.size() == 1);
    REQUIRE(topDownData.at(0).cost.allocations == 15);
    REQUIRE(topDownData.at(0).cost.peak == 94496);
}
