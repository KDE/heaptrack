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

#include "analyze/suppressions.h"
#include "locationdata.h"
#include "parser.h"
#include "treemodel.h"
#include "tst_config.h" // for SRC_DIR

#include <QDebug>
#include <QSignalSpy>

std::ostream& operator<<(std::ostream& os, const QString& value)
{
    os << value.toStdString();
    return os;
}

struct TestParser
{
    TestParser()
        : spySummary(&parser, &Parser::summaryAvailable)
        , spyCCD(&parser, &Parser::callerCalleeDataAvailable)
        , spyBottomUp(&parser, &Parser::bottomUpDataAvailable)
        , spyTopDown(&parser, &Parser::topDownDataAvailable)
        , spyFinished(&parser, &Parser::finished)
    {
        QObject::connect(&parser, &Parser::bottomUpDataAvailable, &parser,
                         [this](const auto& data) { resultData = data.resultData; });
    }

    ~TestParser()
    {
        if (spyFinished.isEmpty())
            REQUIRE(spyFinished.wait());
    }

    CallerCalleeResults awaitCallerCallee()
    {
        if (spyCCD.isEmpty())
            REQUIRE(spyCCD.wait());

        auto ccr = spyCCD.at(0).at(0).value<CallerCalleeResults>();
        REQUIRE(ccr.resultData);
        REQUIRE(ccr.resultData == resultData);
        return ccr;
    }

    TreeData awaitBottomUp()
    {
        if (spyBottomUp.isEmpty())
            REQUIRE(spyBottomUp.wait());

        auto bottomUpData = spyBottomUp.at(0).at(0).value<TreeData>();
        REQUIRE(bottomUpData.resultData);
        REQUIRE(bottomUpData.resultData == resultData);

        if (qEnvironmentVariableIntValue("HEAPTRACK_DEBUG")) {
            qDebug() << "Bottom Up Data:";
            for (const RowData& row : bottomUpData.rows) {
                qDebug() << symbolToString(row.symbol);
            }
        }

        return bottomUpData;
    }

    TreeData awaitTopDown()
    {
        if (spyTopDown.isEmpty())
            REQUIRE(spyTopDown.wait());

        auto topDownData = spyTopDown.at(0).at(0).value<TreeData>();
        REQUIRE(topDownData.resultData);
        REQUIRE(topDownData.resultData == resultData);

        if (qEnvironmentVariableIntValue("HEAPTRACK_DEBUG")) {
            qDebug() << "Top Down Data:";
            for (const RowData& row : topDownData.rows) {
                qDebug() << symbolToString(row.symbol);
            }
        }

        return topDownData;
    }

    SummaryData awaitSummary()
    {
        if (spySummary.isEmpty())
            REQUIRE(spySummary.wait());

        return spySummary.at(0).at(0).value<SummaryData>();
    }

    Parser parser;
    std::shared_ptr<const ResultData> resultData;

    QString symbolToString(const Symbol& sym) const
    {
        const auto module = resultData->string(sym.moduleId);
        return resultData->string(sym.functionId) + '|' + Util::basename(module) + '|' + module;
    }

    QList<Symbol> sortedSymbols(const CallerCalleeResults& ccr) const
    {
        auto ccrSymbolList = ccr.entries.keys();
        std::sort(ccrSymbolList.begin(), ccrSymbolList.end(), [&](const Symbol& lhs, const Symbol& rhs) {
            // keep unresolved functions up front
            auto sortable = [&](const Symbol& symbol) {
                auto str = [&](StringIndex stringId) { return ccr.resultData->string(stringId); };
                return std::make_tuple(str(symbol.functionId), str(symbol.moduleId));
            };
            return sortable(lhs) < sortable(rhs);
        });
        if (qEnvironmentVariableIntValue("HEAPTRACK_DEBUG")) {
            qDebug() << "Sorted Symbols";
            int i = 0;
            for (const Symbol& sym : ccrSymbolList) {
                qDebug() << i++ << symbolToString(sym);
            }
        }
        return ccrSymbolList;
    }

private:
    QSignalSpy spySummary;
    QSignalSpy spyCCD;
    QSignalSpy spyBottomUp;
    QSignalSpy spyTopDown;
    QSignalSpy spyFinished;
};

TEST_CASE ("heaptrack.david.18594.gz", "[parser]") {
    TestParser parser;

    FilterParameters params;
    bool parsedSuppressions = false;
    params.suppressions = parseSuppressions(SRC_DIR "/suppressions.txt", &parsedSuppressions);
    REQUIRE(parsedSuppressions);

    parser.parser.parse(SRC_DIR "/heaptrack.david.18594.gz", QString(), params);

    // ---- Check Caller Callee Data

    const auto ccr = parser.awaitCallerCallee();
    const auto ccrSymbolList = parser.sortedSymbols(ccr);

    // Let's check a few items
    REQUIRE(parser.symbolToString(ccrSymbolList.at(0)) == "<unresolved function>||");
    REQUIRE(parser.symbolToString(ccrSymbolList.at(1))
            == "<unresolved function>|ld-linux-x86-64.so.2|/lib64/ld-linux-x86-64.so.2");
    REQUIRE(parser.symbolToString(ccrSymbolList.at(25))
            == "QByteArray::constData() const|libQt5Core.so.5|/d/qt/5/kde/build/qtbase/lib/libQt5Core.so.5");
    const int lastIndx = ccrSymbolList.size() - 1;
    REQUIRE(parser.symbolToString(ccrSymbolList.at(lastIndx))
            == "~QVarLengthArray|libQt5Core.so.5|/d/qt/5/kde/build/qtbase/lib/libQt5Core.so.5");

    REQUIRE(ccr.entries.count() == 365);
    REQUIRE(ccr.resultData->totalCosts().allocations == 2896);

    // ---- Check Bottom Up Data

    const auto bottomUpData = parser.awaitBottomUp();

    REQUIRE(bottomUpData.rows.size() == 54);
    REQUIRE(parser.symbolToString(bottomUpData.rows.at(3).symbol)
            == "<unresolved function>|libglib-2.0.so.0|/usr/lib64/libglib-2.0.so.0");
    REQUIRE(bottomUpData.rows.at(3).children.size() == 2);
    REQUIRE(bottomUpData.rows.at(3).cost.allocations == 17);
    REQUIRE(bottomUpData.rows.at(3).cost.peak == 2020);
    REQUIRE(parser.symbolToString(bottomUpData.rows.at(53).symbol)
            == "QThreadPool::QThreadPool(QObject*)|libQt5Core.so.5|/d/qt/5/kde/build/qtbase/lib/libQt5Core.so.5");

    // ---- Check Top Down Data

    const auto topDownData = parser.awaitTopDown();
    REQUIRE(topDownData.rows.size() == 5);
    REQUIRE(parser.symbolToString(topDownData.rows.at(2).symbol)
            == "<unresolved function>|ld-linux-x86-64.so.2|/lib64/ld-linux-x86-64.so.2");
    REQUIRE(topDownData.rows.at(2).children.size() == 1);
    REQUIRE(topDownData.rows.at(2).cost.allocations == 15);
    REQUIRE(topDownData.rows.at(2).cost.peak == 94496);

    // ---- Check Summary

    const auto summary = parser.awaitSummary();
    REQUIRE(summary.debuggee == "./david");
    REQUIRE(summary.cost.allocations == 2896);
    REQUIRE(summary.cost.temporary == 729);
    REQUIRE(summary.cost.leaked == 0);
    REQUIRE(summary.totalLeakedSuppressed == 30463);
    REQUIRE(summary.cost.peak == 996970);
    REQUIRE(summary.totalTime == 80);
    REQUIRE(summary.peakRSS == 76042240);
    REQUIRE(summary.peakTime == 0);
    REQUIRE(summary.totalSystemMemory == 16715239424);
    REQUIRE(summary.fromAttached == false);
}

TEST_CASE ("heaptrack.embedded_lsan_suppressions.84207.zst", "[parser]") {
    TestParser parser;

    parser.parser.parse(SRC_DIR "/heaptrack.embedded_lsan_suppressions.84207.zst", QString(), {});

    const auto summary = parser.awaitSummary();
    REQUIRE(summary.debuggee == "./tests/manual/embedded_lsan_suppressions");
    REQUIRE(summary.cost.allocations == 5);
    REQUIRE(summary.cost.temporary == 0);
    REQUIRE(summary.cost.leaked == 5);
    REQUIRE(summary.totalLeakedSuppressed == 5);
    REQUIRE(summary.cost.peak == 72714);
    REQUIRE(summary.totalSystemMemory == 12242059264);
}

TEST_CASE ("heaptrack.embedded_lsan_suppressions.84207.zst without suppressions", "[parser]") {
    TestParser parser;

    FilterParameters params;
    params.disableEmbeddedSuppressions = true;
    parser.parser.parse(SRC_DIR "/heaptrack.embedded_lsan_suppressions.84207.zst", QString(), params);

    const auto summary = parser.awaitSummary();
    REQUIRE(summary.debuggee == "./tests/manual/embedded_lsan_suppressions");
    REQUIRE(summary.cost.allocations == 5);
    REQUIRE(summary.cost.leaked == 10);
    REQUIRE(summary.totalLeakedSuppressed == 0);
}

TEST_CASE ("heaptrack.heaptrack_gui.99454.zst", "[parser]") {
    TestParser parser;

    parser.parser.parse(SRC_DIR "/heaptrack.heaptrack_gui.99454.zst", QString(), {});

    const auto summary = parser.awaitSummary();
    REQUIRE(summary.debuggee == "heaptrack_gui heaptrack.trest_c.78689.zst");
    REQUIRE(summary.cost.allocations == 278534);
    REQUIRE(summary.cost.temporary == 35481);
    REQUIRE(summary.cost.leaked == 1047379);
    REQUIRE(summary.cost.peak == 12222213);
}

TEST_CASE ("heaptrack.heaptrack_gui.99529.zst", "[parser]") {
    TestParser parser;

    parser.parser.parse(SRC_DIR "/heaptrack.heaptrack_gui.99529.zst", QString(), {});

    const auto summary = parser.awaitSummary();
    REQUIRE(summary.debuggee == "heaptrack_gui heaptrack.test_c.78689.zst");
    REQUIRE(summary.cost.allocations == 315255);
    REQUIRE(summary.cost.temporary == 40771);
    REQUIRE(summary.cost.leaked == 1046377);
    REQUIRE(summary.cost.peak == 64840134);
}

TEST_CASE ("heaptrack.heaptrack_gui.{99454,99529}.zst diff", "[parser]") {
    TestParser parser;

    parser.parser.parse(SRC_DIR "/heaptrack.heaptrack_gui.99529.zst", SRC_DIR "/heaptrack.heaptrack_gui.99454.zst", {});

    const auto summary = parser.awaitSummary();
    REQUIRE(summary.debuggee == "heaptrack_gui heaptrack.test_c.78689.zst");
    REQUIRE(summary.cost.allocations == 36721);
    REQUIRE(summary.cost.temporary == 5290);
    REQUIRE(summary.cost.leaked == -1002);
    REQUIRE(summary.cost.peak == 52617921);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    qRegisterMetaType<CallerCalleeResults>();

    const int res = Catch::Session().run(argc, argv);
    return (res < 0xff ? res : 0xff);
}
