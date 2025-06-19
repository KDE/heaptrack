/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <QApplication>
#include <QCommandLineParser>

#include <KAboutData>
#include <KLocalizedString>
#include <kcoreaddons_version.h>

#include "analyze/suppressions.h"
#include "util/config.h"

#include "gui_config.h"
#include "mainwindow.h"
#include "proxystyle.h"

#include <KIconTheme>
#include <QFile>
#include <QResource>

// FIXME: patch KIconTheme so that this isn't needed here
void Q_DECL_UNUSED initRCCIconTheme()
{
    const QString iconThemeRcc = qApp->applicationDirPath() + QStringLiteral("/../share/icons/breeze/breeze-icons.rcc");
    if (!QFile::exists(iconThemeRcc)) {
        qWarning("cannot find icons rcc: %ls", qUtf16Printable(iconThemeRcc));
        return;
    }

    const QString iconThemeName = QStringLiteral("kf5_rcc_theme");
    const QString iconSubdir = QStringLiteral("/icons/") + iconThemeName;
    if (!QResource::registerResource(iconThemeRcc, iconSubdir)) {
        qWarning("Invalid rcc file: %ls", qUtf16Printable(iconThemeRcc));
    }

    if (!QFile::exists(QLatin1Char(':') + iconSubdir + QStringLiteral("/index.theme"))) {
        qWarning("No index.theme found in %ls", qUtf16Printable(iconThemeRcc));
        QResource::unregisterResource(iconThemeRcc, iconSubdir);
    }

    // Tell Qt about the theme
    // Note that since qtbase commit a8621a3f8, this means the QPA (i.e. KIconLoader) will NOT be used.
    QIcon::setThemeName(iconThemeName); // Qt looks under :/icons automatically
    // Tell KIconTheme about the theme, in case KIconLoader is used directly
    KIconTheme::forceThemeForTests(iconThemeName);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setStyle(new ProxyStyle);

#if APPIMAGE_BUILD
    initRCCIconTheme();
#endif

    KLocalizedString::setApplicationDomain("heaptrack");

    KAboutData aboutData(QStringLiteral("heaptrack_gui"), i18n("Heaptrack GUI"),
                         QStringLiteral(HEAPTRACK_VERSION_STRING), i18n("A visualizer for heaptrack data files."),
                         KAboutLicense::LGPL, i18n("Copyright 2015, Milian Wolff <mail@milianw.de>"), QString(),
                         QStringLiteral("mail@milianw.de"));

    aboutData.addAuthor(i18n("Milian Wolff"), i18n("Original author, maintainer"), QStringLiteral("mail@milianw.de"),
                        QStringLiteral("http://milianw.de"));

    aboutData.setOrganizationDomain("kde.org");
    aboutData.setDesktopFileName(QStringLiteral("org.kde.heaptrack"));
    KAboutData::setApplicationData(aboutData);
    app.setWindowIcon(QIcon(QStringLiteral(":/512-heaptrack_app_icon.png")));

    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);

    QCommandLineOption diffOption {{QStringLiteral("d"), QStringLiteral("diff")},
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
    QCommandLineOption disableBuiltinSuppressionsOption {
        {QStringLiteral("disable-builtin-suppressions")},
        i18n(
            "Ignore suppression definitions that are built into heaptrack. By default, heaptrack will suppress certain "
            "known leaks in common system libraries.")};
    parser.addOption(disableBuiltinSuppressionsOption);
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
        window->setDisableBuiltinSuppressions(parser.isSet(disableBuiltinSuppressionsOption));
        window->show();
        return window;
    };

    const auto files = parser.positionalArguments();
    for (const auto& file : files) {
        createWindow()->loadFile(file, parser.value(diffOption));
    }

    if (files.isEmpty()) {
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
