/*
    SPDX-FileCopyrightText: 2024 Aravind Vijayan <aravindev@live.in>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "analyze/gui/util.h"
#include <QString>
#include <QtTest>

class TestTemplateElision : public QObject
{
    Q_OBJECT

private slots:
    void testTemplateElision();
    void testTemplateElision_data();
};

void TestTemplateElision::testTemplateElision()
{
    QFETCH(QString, test_case);
    QFETCH(QString, result);
    QCOMPARE(result, Util::elideTemplateArguments(test_case));
}

void TestTemplateElision::testTemplateElision_data()
{
    QTest::addColumn<QString>("test_case");
    QTest::addColumn<QString>("result");

    QTest::newRow("simple_case") << "MainWindow::onLoadingFinish(unsigned int&)"
                                 << "MainWindow::onLoadingFinish(unsigned int&)";
    QTest::newRow("one_bracket") << "std::vector<test type in bracket> MainWindow::onLoadingFinish(unsigned int&)"
                                 << "std::vector<> MainWindow::onLoadingFinish(unsigned int&)";
    QTest::newRow("two_brackets") << "std::vector<test type in bracket> MainWindow<vector_a>::onLoadingFinish(unsigned int&)"
                                 << "std::vector<> MainWindow<>::onLoadingFinish(unsigned int&)";
    QTest::newRow("nested_brackets") << "std::vector<test type <int> in bracket> MainWindow::onLoadingFinish(unsigned int&)"
                                 << "std::vector<> MainWindow::onLoadingFinish(unsigned int&)";
}

QTEST_APPLESS_MAIN(TestTemplateElision)

#include "tst_template_elision.moc"