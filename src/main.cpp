#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "utils/AppTranslator.h"
#include "ui/mainwindow/MainWindow.h"
#include "utils/SingleInstance.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QFont>
#include <QMessageBox>
#include <QObject>

int main(int argc, char* argv[])
{
    QCoreApplication::setApplicationName(QStringLiteral("RiipL"));
    QCoreApplication::setApplicationVersion(RIIPL_BUILD_VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Local DeepL rip-off powered by any OpenAI-compatible API."));
    const QCommandLineOption helpOption = parser.addHelpOption();
    const QCommandLineOption versionOption = parser.addVersionOption();

    // Answered before QApplication, which cannot start without a display. The list
    // is rebuilt because QCoreApplication::arguments() needs an application object,
    // and Qt's own options are still in it, so a parse failure is not an error.
    QStringList arguments;
    arguments.reserve(argc);
    for (int index = 0; index < argc; ++index)
        arguments.append(QString::fromLocal8Bit(argv[index]));

    parser.parse(arguments);
    if (parser.isSet(helpOption) || parser.isSet(versionOption)) {
        // Gives the usage line its executable name.
        QCoreApplication reporter(argc, argv);
        if (parser.isSet(versionOption))
            parser.showVersion();
        parser.showHelp();
    }

    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("riipl.desktop"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    // Judged against argv as QApplication left it, without the options Qt owns.
    parser.process(app);

    ConfigManager::createInstance();
    ConfigManager* config = ConfigManager::instance();

    AppTranslator translator;
    translator.apply(config->uiLocale());

    QFont font = app.font();
    font.setPointSize(config->intValue(Keys::uiFontSize));
    app.setFont(font);

    // The translator dies before the application, so it is the connection's context.
    QObject::connect(config, &ConfigManager::changed, &translator,
        [&translator, config](const QString& key) {
            if (key == Keys::uiLanguage)
                translator.apply(config->uiLocale());
        });

    QObject::connect(config, &ConfigManager::changed, &app,
        [config](const QString& key) {
            if (key != Keys::uiFontSize)
                return;
            QFont updated = QApplication::font();
            updated.setPointSize(config->intValue(key));
            QApplication::setFont(updated);
        });

    SingleInstance singleInstance;
    switch (singleInstance.tryLock()) {
    case SingleInstance::Role::Secondary:
        if (!singleInstance.notifyExistingInstance()) {
            QMessageBox::warning(
                nullptr, QStringLiteral("RiipL"),
                QCoreApplication::translate(
                    "main", "RiipL is already running and did not answer the activation request."));
            return 1;
        }
        return 0;
    case SingleInstance::Role::Error:
        qWarning() << "Failed to establish the single-instance IPC channel";
        return 1;
    case SingleInstance::Role::Primary:
        break;
    }

    MainWindow window;
    window.show();

    // With a StatusNotifierItem tray icon (KDE plasma-integration), Qt's
    // built-in quit-on-last-window-closed leaves the process running even
    // though lastWindowClosed is emitted; wire the signal to quit() explicitly.
    QObject::connect(&app, &QGuiApplication::lastWindowClosed,
                     &app, &QCoreApplication::quit);

    QObject::connect(&singleInstance, &SingleInstance::activationRequested, &window,
        [&window]() {
            window.show();
            window.raise();
            window.activateWindow();
        });

    QObject::connect(&app, &QCoreApplication::aboutToQuit, config, &ConfigManager::flush);

    return app.exec();
}
