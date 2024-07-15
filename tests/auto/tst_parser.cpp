/*
    SPDX-FileCopyrightText: 2019 David Faure <david.faure@kdab.com>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#define DOCTEST_CONFIG_IMPLEMENT
#include "3rdparty/doctest.h"

#include "analyze/suppressions.h"
#include "locationdata.h"
#include "parser.h"
#include "treemodel.h"
#include "tst_config.h" // for SRC_DIR

#include <KLocalizedString>

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
        , spyFailedToOpen(&parser, &Parser::failedToOpen)
    {
        QObject::connect(&parser, &Parser::bottomUpDataAvailable, &parser,
                         [this](const auto& data) { resultData = data.resultData; });
    }

    ~TestParser()
    {
        if (spyFinished.isEmpty() && spyFailedToOpen.isEmpty())
            REQUIRE(spyFinished.wait(20000));
    }

    CallerCalleeResults awaitCallerCallee()
    {
        if (spyCCD.isEmpty())
            REQUIRE(spyCCD.wait(20000));

        auto ccr = spyCCD.at(0).at(0).value<CallerCalleeResults>();
        REQUIRE(ccr.resultData);
        REQUIRE(ccr.resultData == resultData);
        return ccr;
    }

    TreeData awaitBottomUp()
    {
        if (spyBottomUp.isEmpty())
            REQUIRE(spyBottomUp.wait(20000));

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
            REQUIRE(spyTopDown.wait(20000));

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
            REQUIRE(spySummary.wait(20000));

        return spySummary.at(0).at(0).value<SummaryData>();
    }

    QString awaitError()
    {
        if (spyFailedToOpen.isEmpty())
            REQUIRE(spyFailedToOpen.wait(20000));

        return spyFailedToOpen.at(0).at(0).toString();
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
    QSignalSpy spyFailedToOpen;
};

TEST_CASE ("heaptrack.david.18594.gz") {
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

TEST_CASE ("heaptrack.embedded_lsan_suppressions.84207.zst") {
    TestParser parser;

    parser.parser.parse(SRC_DIR "/heaptrack.embedded_lsan_suppressions.84207.zst", QString(), {});

    const auto summary = parser.awaitSummary();
    REQUIRE(summary.debuggee == "./tests/manual/embedded_lsan_suppressions");
    REQUIRE(summary.cost.allocations == 5);
    REQUIRE(summary.cost.temporary == 0);
    REQUIRE(summary.cost.leaked == 5);
    REQUIRE(summary.totalLeakedSuppressed == 5);
    REQUIRE(summary.cost.peak == 72714);
    REQUIRE(summary.totalSystemMemory == 33643876352);
}

TEST_CASE ("heaptrack.embedded_lsan_suppressions.84207.zst without suppressions") {
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

TEST_CASE ("heaptrack.heaptrack_gui.99454.zst") {
    TestParser parser;

    FilterParameters params;
    params.disableBuiltinSuppressions = true;

    parser.parser.parse(SRC_DIR "/heaptrack.heaptrack_gui.99454.zst", QString(), params);

    const auto summary = parser.awaitSummary();
    REQUIRE(summary.debuggee == "heaptrack_gui heaptrack.trest_c.78689.zst");
    REQUIRE(summary.cost.allocations == 278534);
    REQUIRE(summary.cost.temporary == 35481);
    REQUIRE(summary.cost.leaked == 1047379);
    REQUIRE(summary.cost.peak == 12222213);

    const auto ccr = parser.awaitCallerCallee();
    const auto sortedSymbols = parser.sortedSymbols(ccr);

    const auto& sym = sortedSymbols[994];
    REQUIRE(parser.symbolToString(sym) == "QHashData::allocateNode(int)|libQt5Core.so.5|/usr/lib/libQt5Core.so.5");
    const auto& cost = ccr.entries[sym];
    CHECK(cost.inclusiveCost.allocations == 5214);
    CHECK(cost.inclusiveCost.temporary == 0);
    CHECK(cost.inclusiveCost.leaked == 32);
    CHECK(cost.inclusiveCost.peak == 56152);
    CHECK(cost.selfCost.allocations == 5214);
    CHECK(cost.selfCost.temporary == 0);
    CHECK(cost.selfCost.leaked == 32);
    CHECK(cost.selfCost.peak == 56152);
}

TEST_CASE ("heaptrack.heaptrack_gui.99529.zst") {
    TestParser parser;

    FilterParameters params;
    params.disableBuiltinSuppressions = true;

    parser.parser.parse(SRC_DIR "/heaptrack.heaptrack_gui.99529.zst", QString(), params);

    const auto summary = parser.awaitSummary();
    REQUIRE(summary.debuggee == "heaptrack_gui heaptrack.test_c.78689.zst");
    REQUIRE(summary.cost.allocations == 315255);
    REQUIRE(summary.cost.temporary == 40771);
    REQUIRE(summary.cost.leaked == 1046377);
    REQUIRE(summary.cost.peak == 64840134);

    const auto ccr = parser.awaitCallerCallee();
    const auto sortedSymbols = parser.sortedSymbols(ccr);

    const auto& sym = sortedSymbols[1103];
    REQUIRE(parser.symbolToString(sym) == "QHashData::allocateNode(int)|libQt5Core.so.5|/usr/lib/libQt5Core.so.5");
    const auto& cost = ccr.entries[sym];
    CHECK(cost.inclusiveCost.allocations == 5559);
    CHECK(cost.inclusiveCost.temporary == 0);
    CHECK(cost.inclusiveCost.leaked == 32);
    CHECK(cost.inclusiveCost.peak == 68952);
    CHECK(cost.selfCost.allocations == 5559);
    CHECK(cost.selfCost.temporary == 0);
    CHECK(cost.selfCost.leaked == 32);
    CHECK(cost.selfCost.peak == 68952);
}

TEST_CASE ("heaptrack.heaptrack_gui.{99454,99529}.zst diff") {
    TestParser parser;

    parser.parser.parse(SRC_DIR "/heaptrack.heaptrack_gui.99529.zst", SRC_DIR "/heaptrack.heaptrack_gui.99454.zst", {});

    const auto summary = parser.awaitSummary();
    REQUIRE(summary.debuggee == "heaptrack_gui heaptrack.test_c.78689.zst");
    REQUIRE(summary.cost.allocations == 36721);
    REQUIRE(summary.cost.temporary == 5290);
    REQUIRE(summary.cost.leaked == -1002);
    REQUIRE(summary.cost.peak == 52617921);

    const auto ccr = parser.awaitCallerCallee();
    const auto sortedSymbols = parser.sortedSymbols(ccr);

    const auto& sym = sortedSymbols[545];
    REQUIRE(parser.symbolToString(sym) == "QHashData::allocateNode(int)|libQt5Core.so.5|/usr/lib/libQt5Core.so.5");
    const auto& cost = ccr.entries[sym];
    CHECK(cost.inclusiveCost.allocations == (5559 - 5214));
    CHECK(cost.inclusiveCost.temporary == 0);
    CHECK(cost.inclusiveCost.leaked == 0);
    CHECK(cost.inclusiveCost.peak == (68952 - 56152));
    CHECK(cost.selfCost.allocations == (5559 - 5214));
    CHECK(cost.selfCost.temporary == 0);
    CHECK(cost.selfCost.leaked == 0);
    CHECK(cost.selfCost.peak == ((68952 - 56152)));
}

TEST_CASE ("heaptrack.test_sysroot.raw") {
    TestParser parser;

    parser.parser.parse(SRC_DIR "/test_sysroot/heaptrack.test_sysroot.raw", {}, {});

    const auto error = parser.awaitError();
    REQUIRE(error.contains("heaptrack.test_sysroot.raw"));
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    qRegisterMetaType<CallerCalleeResults>();
    KLocalizedString::setApplicationDomain("heaptrack");

    doctest::Context context;
    context.applyCommandLine(argc, argv);

    return context.run();
}
