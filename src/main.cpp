#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "ui/mainwindow/MainWindow.h"
#include "utils/SingleInstance.h"

#include <QApplication>
#include <QDebug>
#include <QFont>
#include <QLibraryInfo>
#include <QObject>
#include <QTranslator>

namespace {

class AppTranslator
{
public:
    void apply(const QString& language)
    {
        for (QTranslator* translator : {&m_app, &m_qt}) {
            qApp->removeTranslator(translator);
        }
        if (language == QLatin1String("zh")) {
            if (m_app.load(QStringLiteral(":/i18n/riip_zh.qm")))
                qApp->installTranslator(&m_app);
            if (m_qt.load(QStringLiteral("qtbase_zh_CN"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
                qApp->installTranslator(&m_qt);
        }
    }

private:
    QTranslator m_app;
    QTranslator m_qt;
};

}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("RiipL"));
    QApplication::setApplicationName(QStringLiteral("RiipL"));
    QApplication::setApplicationVersion(RIIP_VERSION);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));

    ConfigManager::createInstance();
    ConfigManager* config = ConfigManager::instance();

    AppTranslator translator;
    translator.apply(config->resolvedUiLanguage());

    QFont font = app.font();
    font.setPointSize(config->intValue(Keys::uiFontSize));
    app.setFont(font);

    QObject::connect(config, &ConfigManager::changed, &app,
        [&translator, config](const QString& key) {
            if (key == Keys::uiLanguage)
                translator.apply(config->resolvedUiLanguage());
            else if (key == Keys::uiFontSize) {
                QFont updated = QApplication::font();
                updated.setPointSize(config->intValue(key));
                QApplication::setFont(updated);
            }
        });

    SingleInstance singleInstance;
    switch (singleInstance.tryLock()) {
    case SingleInstance::Role::Secondary:
        singleInstance.notifyExistingInstance();
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
