/*
 * Copyright 2015-2017 Milian Wolff <mail@milianw.de>
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

#include <QApplication>
#include <QCommandLineParser>

#include <kcoreaddons_version.h>
#include <KAboutData>
#include <KLocalizedString>

#include "analyze/suppressions.h"

#include "gui_config.h"
#include "mainwindow.h"
#include "proxystyle.h"

int main(int argc, char** argv)
{
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication app(argc, argv);
    app.setStyle(new ProxyStyle);

#if APPIMAGE_BUILD
    QIcon::setThemeSearchPaths({app.applicationDirPath() + QLatin1String("/../share/icons/")});
    QIcon::setThemeName(QStringLiteral("breeze"));
#endif

    KLocalizedString::setApplicationDomain("heaptrack");

    KAboutData aboutData(QStringLiteral("heaptrack_gui"), i18n("Heaptrack GUI"), QStringLiteral("0.1"),
                         i18n("A visualizer for heaptrack data files."), KAboutLicense::LGPL,
                         i18n("Copyright 2015, Milian Wolff <mail@milianw.de>"), QString(),
                         QStringLiteral("mail@milianw.de"));

    aboutData.addAuthor(i18n("Milian Wolff"), i18n("Original author, maintainer"), QStringLiteral("mail@milianw.de"),
                        QStringLiteral("http://milianw.de"));

    aboutData.setOrganizationDomain("kde.org");
#if KCOREADDONS_VERSION >= QT_VERSION_CHECK(5, 16, 0)
    aboutData.setDesktopFileName(QStringLiteral("org.kde.heaptrack"));
#endif
    KAboutData::setApplicationData(aboutData);
    app.setWindowIcon(QIcon(QStringLiteral(":/512-heaptrack_app_icon.png")));

    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);

    QCommandLineOption diffOption{{QStringLiteral("d"), QStringLiteral("diff")},
                                  i18n("Base profile data to compare other files to."),
                                  QStringLiteral("<file>")};
    parser.addOption(diffOption);
    QCommandLineOption suppressionsOption {
        {QStringLiteral("s"), QStringLiteral("suppressions")},
        i18n("Load list of leak suppressions from the specified file. Specify one suppression per line, and start each "
             "line with 'leak:', i.e. use the LSAN suppression file format."),
        QStringLiteral("<file>")};
    parser.addOption(suppressionsOption);
    QCommandLineOption disableEmbeddedSuppressionsOption {
        {QStringLiteral("disable-embedded-suppressions")},
        i18n("Ignore suppression definitions that are embedded into the heaptrack data file. By default, heaptrack "
             "will copy the suppressions optionally defined via a `const char *__lsan_default_suppressions()` symbol "
             "in the debuggee application.  These are then always applied when analyzing the data, unless this feature "
             "is explicitly disabled using this command line option.")};
    parser.addOption(disableEmbeddedSuppressionsOption);
    parser.addPositionalArgument(QStringLiteral("files"), i18n("Files to load"), i18n("[FILE...]"));

    parser.process(app);
    aboutData.processCommandLine(&parser);

    bool parsedOk = false;
    const auto suppressions = parseSuppressions(parser.value(suppressionsOption).toStdString(), &parsedOk);
    if (!parsedOk) {
        return 1;
    }

    auto createWindow = [&]() -> MainWindow* {
        auto window = new MainWindow;
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->setSuppressions(suppressions);
        window->setDisableEmbeddedSuppressions(parser.isSet(disableEmbeddedSuppressionsOption));
        window->show();
        return window;
    };

    foreach (const QString& file, parser.positionalArguments()) {
        createWindow()->loadFile(file, parser.value(diffOption));
    }

    if (parser.positionalArguments().isEmpty()) {
        createWindow();
    }

#if APPIMAGE_BUILD
    // cleanup the environment when we are running from within the AppImage
    // to allow launching system applications using Qt without them loading
    // the bundled Qt we ship in the AppImage
    auto LD_LIBRARY_PATH = qgetenv("LD_LIBRARY_PATH");
    LD_LIBRARY_PATH.remove(0, LD_LIBRARY_PATH.indexOf(':') + 1);
    qputenv("LD_LIBRARY_PATH", LD_LIBRARY_PATH);
#endif

    return app.exec();
}
