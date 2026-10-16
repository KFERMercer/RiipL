#include "ConfigManager.h"
#include "Defaults.h"
#include "core/json/JsonUtils.h"
#include "utils/AppLog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>

ConfigManager* ConfigManager::s_instance = nullptr;

ConfigManager* ConfigManager::instance()
{
    if (!s_instance)
        createInstance();
    return s_instance;
}

void ConfigManager::createInstance(const QString& configDir)
{
    QString dir = configDir;
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (s_instance) {
        s_instance->reopen(dir);
        return;
    }
    s_instance = new ConfigManager(dir);
}

ConfigManager::ConfigManager(const QString& configDir)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &ConfigManager::save);
    reopen(configDir);
}

void ConfigManager::reopen(const QString& configDir)
{
    flush();
    m_dir = configDir;
    // load() leaves the values in place when it cannot read the file.
    m_user = QJsonObject();
    if (!QDir().mkpath(m_dir))
        AppLog::warning() << "cannot create config directory" << m_dir;
    load();
}

QString ConfigManager::configFilePath() const
{
    return m_dir + QStringLiteral("/config.json");
}

QString ConfigManager::historyFilePath() const
{
    return m_dir + QStringLiteral("/history.json");
}

void ConfigManager::load()
{
    QFile file(configFilePath());
    if (!file.exists()) {
        save();
        return;
    }
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        quarantineCorruptFile();
        return;
    }
    m_user = doc.object();
}

void ConfigManager::quarantineCorruptFile()
{
    const QString path = configFilePath();
    const QString stamp =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString backup = path + QStringLiteral(".corrupt-") + stamp + QStringLiteral(".bak");
    if (!QFile::rename(path, backup)) {
        AppLog::warning() << "cannot set aside the corrupted config file" << path;
        return;
    }
    AppLog::warning() << "config.json is corrupted, falling back to defaults; kept as" << backup;
}

void ConfigManager::save()
{
    QSaveFile file(configFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        AppLog::warning() << "cannot write config file" << file.fileName() << file.errorString();
        return;
    }
    const QByteArray data = QJsonDocument(m_user).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        AppLog::warning() << "cannot write config file" << file.fileName() << file.errorString();
        return;
    }
    if (!file.commit()) {
        AppLog::warning() << "cannot commit config file" << file.fileName() << file.errorString();
        return;
    }
    if (!QFile::setPermissions(configFilePath(), QFile::ReadOwner | QFile::WriteOwner))
        AppLog::warning() << "cannot restrict config file permissions" << configFilePath();
}

void ConfigManager::scheduleSave()
{
    m_saveTimer.start();
}

QJsonValue ConfigManager::value(const QString& key) const
{
    const QJsonValue userValue = JsonUtils::valueAt(m_user, key);
    if (!userValue.isUndefined())
        return userValue;
    return Defaults::value(key);
}

QString ConfigManager::stringValue(const QString& key) const
{
    return value(key).toString();
}

QLocale ConfigManager::uiLocale() const
{
    const QString stored = stringValue(Keys::uiLanguage);
    const QLocale system = QLocale::system();
    if (!Keys::uiLanguageCodes().contains(stored))
        return system.language() == QLocale::Chinese ? system
                                                     : QLocale(QLocale::English, QLocale::AnyCountry);
    const QLocale chosen(stored);
    // The session supplies the script when it speaks the chosen language.
    return chosen.language() == system.language() ? system : chosen;
}

bool ConfigManager::boolValue(const QString& key) const
{
    return value(key).toBool();
}

int ConfigManager::intValue(const QString& key) const
{
    return value(key).toInt();
}

double ConfigManager::doubleValue(const QString& key) const
{
    return value(key).toDouble();
}

bool ConfigManager::isDefault(const QString& key) const
{
    return value(key) == Defaults::value(key);
}

void ConfigManager::setValue(const QString& key, const QJsonValue& value)
{
    // The pickers hold no entry for a code outside the offered set.
    if (key == Keys::uiLanguage && !Keys::isOfferedUiLanguage(value.toString())) {
        AppLog::warning() << "ignoring unsupported ui.language" << value.toString();
        return;
    }
    if (value == Defaults::value(key)) {
        removeValue(key);
        return;
    }
    if (JsonUtils::valueAt(m_user, key) == value)
        return;
    JsonUtils::setByPath(m_user, key, value);
    scheduleSave();
    emit changed(key);
}

void ConfigManager::removeValue(const QString& key)
{
    if (JsonUtils::valueAt(m_user, key).isUndefined())
        return;
    JsonUtils::removeByPath(m_user, key);
    scheduleSave();
    emit changed(key);
}

void ConfigManager::flush()
{
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        save();
    }
}
