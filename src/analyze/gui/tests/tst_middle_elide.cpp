#include <QtTest>
#include <QString>
#include "analyze/gui/middleelide.h"

// add necessary includes here

const char* simple_case = "MainWindow::onLoadingFinish(unsigned int&)";
const char* one_bracket = "std::vector<test type in bracket> MainWindow::onLoadingFinish(unsigned int&)";
const char* one_bracket_fixed = "std::vector<...> MainWindow::onLoadingFinish(unsigned int&)";
const char* two_brackets = "std::vector<test type in bracket> MainWindow<vector_a>::onLoadingFinish(unsigned int&)";
const char* two_brackets_fixed = "std::vector<...> MainWindow<...>::onLoadingFinish(unsigned int&)";
const char* nested_brackets = "std::vector<test type <int> in bracket> MainWindow::onLoadingFinish(unsigned int&)";
const char* nested_brackets_fixed = "std::vector<...> MainWindow::onLoadingFinish(unsigned int&)";


class test_initilaize : public QObject
{
    Q_OBJECT

public:
    test_initilaize() = default;
    ~test_initilaize() = default;

private slots:
    void test_simple_case();
    void test_single_bracket();
    void test_multiple_brackets();
    void test_nested_brackets();
};

void test_initilaize::test_simple_case()
{
    QString testString = QString::fromUtf8(simple_case);
    QString result = MiddleElide::elideAngleBracket(testString);
    QVERIFY(result == testString);
}

void test_initilaize::test_single_bracket()
{
    QString testString = QString::fromUtf8(one_bracket);
    QString testStringFixed = QString::fromUtf8(one_bracket_fixed);
    QString result = MiddleElide::elideAngleBracket(testString);
    QVERIFY(result == testStringFixed);
}

void test_initilaize::test_multiple_brackets()
{
    QString testString = QString::fromUtf8(two_brackets);
    QString testStringFixed = QString::fromUtf8(two_brackets_fixed);
    QString result = MiddleElide::elideAngleBracket(testString);
    QVERIFY(result == testStringFixed);
}

void test_initilaize::test_nested_brackets()
{
    QString testString = QString::fromUtf8(nested_brackets);
    QString testStringFixed = QString::fromUtf8(nested_brackets_fixed);
    QString result = MiddleElide::elideAngleBracket(testString);
    QVERIFY(result == testStringFixed);
}

QTEST_APPLESS_MAIN(test_initilaize)

#include "tst_middle_elide.moc"
