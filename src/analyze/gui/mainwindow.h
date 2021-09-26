/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include <KSharedConfig>

#include <analyze/filterparameters.h>

namespace Ui {
class MainWindow;
}

class TreeModel;
class ChartModel;
class Parser;
class ResultData;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr);
    virtual ~MainWindow();

public slots:
    void loadFile(const QString& path, const QString& diffBase = {});
    void reparse(int64_t minTime, int64_t maxTime);
    void openNewFile();
    void closeFile();

    void setCodeNavigationIDE(QAction* action);
    void navigateToCode(const QString& url, int lineNumber, int columnNumber = -1);

    void setDisableEmbeddedSuppressions(bool disable);
    void setDisableBuiltinSuppressions(bool disable);
    void setSuppressions(std::vector<std::string> suppressions);

signals:
    void clearData();

private:
    void showError(const QString& message);
    void setupStacks();
    void setupCodeNavigationMenu();

    QScopedPointer<Ui::MainWindow> m_ui;
    Parser* m_parser;
    KSharedConfig::Ptr m_config;
    bool m_diffMode = false;

    QAction* m_openAction = nullptr;
    QAction* m_openNewAction = nullptr;
    QAction* m_closeAction = nullptr;
    QAction* m_quitAction = nullptr;
    QAction* m_disableEmbeddedSuppressions = nullptr;
    QAction* m_disableBuiltinSuppressions = nullptr;
    FilterParameters m_lastFilterParameters;
};

#endif // MAINWINDOW_H
