/*
 * Copyright 2015-2017 Milian Wolff <mail@milianw.de>
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

#include <QApplication>
#include <QCommandLineParser>

#include <KAboutData>
#include <KLocalizedString>

#include "mainwindow.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    KLocalizedString::setApplicationDomain("heaptrack");

    KAboutData aboutData(QStringLiteral("heaptrack_gui"), i18n("Heaptrack GUI"), QStringLiteral("0.1"),
                         i18n("A visualizer for heaptrack data files."), KAboutLicense::LGPL,
                         i18n("Copyright 2015, Milian Wolff <mail@milianw.de>"), QString(), QStringLiteral("mail@milianw.de"));

    aboutData.addAuthor(i18n("Milian Wolff"), i18n("Original author, maintainer"),
                        QStringLiteral("mail@milianw.de"), QStringLiteral("http://milianw.de"));

    aboutData.setOrganizationDomain("kde.org");
    KAboutData::setApplicationData(aboutData);

    app.setApplicationName(aboutData.componentName());
    app.setApplicationDisplayName(aboutData.displayName());
    app.setOrganizationDomain(aboutData.organizationDomain());
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("office-chart-area")));
    app.setApplicationVersion(aboutData.version());

    QCommandLineParser parser;
    KAboutData::setApplicationData(aboutData);
    parser.addVersionOption();
    parser.addHelpOption();
    aboutData.setupCommandLine(&parser);

    QCommandLineOption diffOption {
        { QStringLiteral("d"), QStringLiteral("diff")},
        i18n("Base profile data to compare other files to."),
        QStringLiteral("<file>")
    };
    parser.addOption(diffOption);
    parser.addPositionalArgument(QStringLiteral("files"), i18n( "Files to load" ), i18n("[FILE...]"));

    parser.process(app);
    aboutData.processCommandLine(&parser);

    auto createWindow = [] () -> MainWindow* {
        auto window = new MainWindow;
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->show();
        return window;
    };

    foreach (const QString &file, parser.positionalArguments()) {
        createWindow()->loadFile(file, parser.value(diffOption));
    }

    if (parser.positionalArguments().isEmpty()) {
        createWindow();
    }

    return app.exec();
}
