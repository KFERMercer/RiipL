#include "ConfigManager.h"
#include "Defaults.h"
#include "core/json/JsonUtils.h"

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
    if (s_instance)
        s_instance->flush();
    delete s_instance;
    QString dir = configDir;
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    s_instance = new ConfigManager(dir);
}

ConfigManager::ConfigManager(const QString& configDir)
    : m_dir(configDir)
{
    if (!QDir().mkpath(m_dir))
        qWarning() << "RiipL: cannot create config directory" << m_dir;
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &ConfigManager::save);
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
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("RiipL: config.json is corrupted, falling back to defaults");
        return;
    }
    m_user = doc.object();
    repairUiLanguage();
}

// An unoffered language would make the picker fall back to its first entry and
// report that as an edit, so the stored value is dropped instead.
void ConfigManager::repairUiLanguage()
{
    if (hasValidUiLanguage())
        return;
    JsonUtils::removeByPath(m_user, Keys::uiLanguage);
    scheduleSave();
}

void ConfigManager::save()
{
    QSaveFile file(configFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "RiipL: cannot write config file" << file.fileName() << file.errorString();
        return;
    }
    const QByteArray data = QJsonDocument(m_user).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        qWarning() << "RiipL: cannot write config file" << file.fileName() << file.errorString();
        return;
    }
    if (!file.commit())
        qWarning() << "RiipL: cannot commit config file" << file.fileName() << file.errorString();
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

bool ConfigManager::hasValidUiLanguage() const
{
    return Keys::isOfferedUiLanguage(stringValue(Keys::uiLanguage));
}

QLocale ConfigManager::uiLocale() const
{
    const QString stored = stringValue(Keys::uiLanguage);
    const QLocale system = QLocale::system();
    // No offered language stored: follow the session only when it is Chinese.
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
    // Refused here as on load, so the stored value never disagrees with the
    // selection the picker shows.
    if (key == Keys::uiLanguage && !Keys::isOfferedUiLanguage(value.toString())) {
        qWarning() << "RiipL: ignoring unsupported ui.language" << value.toString();
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
