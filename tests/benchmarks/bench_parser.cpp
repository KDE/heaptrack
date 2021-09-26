/*
    SPDX-FileCopyrightText: 2019 David Faure <david.faure@kdab.com>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "parser.h"

#include "analyze/suppressions.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QCommandLineOption stopAfterOption(
        QStringLiteral("stop-after"),
        QStringLiteral("stop parsing after the given stage, possible values are: Summary, BottomUp, SizeHistogram, "
                       "TopDownAndCallerCallee, Finished"),
        QStringLiteral("stage"), QStringLiteral("Finished"));
    QCommandLineParser commandLineParser;
    commandLineParser.addOption(stopAfterOption);
    commandLineParser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("heaptrack data files to parse"));
    commandLineParser.addHelpOption();

    commandLineParser.process(app);

    const auto files = commandLineParser.positionalArguments();
    if (files.isEmpty())
        return 1;

    qRegisterMetaType<CallerCalleeResults>();
    qRegisterMetaType<TreeData>();

    Parser parser;
    QObject::connect(&parser, &Parser::finished,
                     &app, &QCoreApplication::quit);
    QObject::connect(&parser, &Parser::failedToOpen, &app, [&](const QString& path) {
        qWarning() << "failed to open" << path;
        app.exit(1);
    });

    const auto stopAfter = [&]() {
        const auto str = commandLineParser.value(stopAfterOption);
        if (str == QLatin1String("Summary")) {
            return Parser::StopAfter::Summary;
        } else if (str == QLatin1String("BottomUp")) {
            return Parser::StopAfter::BottomUp;
        } else if (str == QLatin1String("SizeHistogram")) {
            return Parser::StopAfter::SizeHistogram;
        } else if (str == QLatin1String("TopDownAndCallerCallee")) {
            return Parser::StopAfter::TopDownAndCallerCallee;
        } else if (str == QLatin1String("Finished")) {
            return Parser::StopAfter::Finished;
        }

        qWarning() << "unsupported stopAfter stage:" << str;
        exit(1);
    }();

    FilterParameters params;
    const auto suppressionsFile = files.value(2);
    if (!suppressionsFile.isEmpty()) {
        bool parsedOk = false;
        params.suppressions = parseSuppressions(suppressionsFile.toStdString(), &parsedOk);
        if (!parsedOk)
            return 1;
    }

    parser.parse(files.value(0), files.value(1), params, stopAfter);

    return app.exec();
}
