#include "ConfigManager.h"
#include "Defaults.h"
#include "utils/JsonUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>

ConfigManager* ConfigManager::s_instance = nullptr;

ConfigManager* ConfigManager::instance()
{
    return s_instance;
}

void ConfigManager::createInstance(const QString& configDir)
{
    delete s_instance;
    QString dir = configDir;
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        const QString organization = QCoreApplication::organizationName();
        if (!organization.isEmpty())
            dir += QLatin1Char('/') + organization;
    }
    s_instance = new ConfigManager(dir);
}

ConfigManager::ConfigManager(const QString& configDir)
    : m_dir(configDir)
{
    QDir().mkpath(m_dir);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &ConfigManager::save);
    importLegacyFiles();
    load();
}

void ConfigManager::importLegacyFiles()
{
    const QString legacyDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (legacyDir == m_dir || !QFileInfo(legacyDir).exists())
        return;
    const QStringList names = {
        QStringLiteral("config.json"),
        QStringLiteral("history.json")
    };
    for (const QString& name : names) {
        const QString source = legacyDir + QLatin1Char('/') + name;
        const QString target = m_dir + QLatin1Char('/') + name;
        if (!QFileInfo::exists(target) && QFileInfo::exists(source))
            QFile::copy(source, target);
    }
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
}

void ConfigManager::save()
{
    QSaveFile file(configFilePath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(m_user).toJson(QJsonDocument::Indented));
    file.commit();
}

void ConfigManager::scheduleSave()
{
    m_saveTimer.start();
}

QJsonValue ConfigManager::value(const QString& key) const
{
    const QJsonValue userValue = JsonUtils::getByPath(m_user, key);
    if (!userValue.isUndefined())
        return userValue;
    return Defaults::value(key);
}

QString ConfigManager::stringValue(const QString& key) const
{
    return value(key).toString();
}

QString ConfigManager::resolvedUiLanguage() const
{
    const QString value = stringValue(Keys::uiLanguage);
    if (value == QLatin1String("zh") || value == QLatin1String("en"))
        return value;
    const QLocale locale = QLocale::system();
    return locale.language() == QLocale::Chinese ? QStringLiteral("zh") : QStringLiteral("en");
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
    return JsonUtils::equals(value(key), Defaults::value(key));
}

void ConfigManager::setValue(const QString& key, const QJsonValue& value)
{
    if (JsonUtils::equals(value, Defaults::value(key))) {
        removeValue(key);
        return;
    }
    JsonUtils::setByPath(m_user, key, value);
    scheduleSave();
    emit changed(key);
}

void ConfigManager::removeValue(const QString& key)
{
    if (!JsonUtils::getByPath(m_user, key).isUndefined()) {
        JsonUtils::removeByPath(m_user, key);
        scheduleSave();
    }
    emit changed(key);
}

void ConfigManager::resetTo(const QJsonObject& doc)
{
    m_user = doc;
    scheduleSave();
    const QStringList keys = Defaults::allKeys();
    for (const QString& key : keys)
        emit changed(key);
}

void ConfigManager::flush()
{
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        save();
    }
}

QString ConfigManager::placeholderFor(const QString& key) const
{
    const QJsonValue def = Defaults::value(key);
    QString text;
    if (def.isString())
        text = def.toString();
    else if (def.isBool())
        text = def.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    else if (def.isDouble())
        text = JsonUtils::compactJson(def);
    if (text.isEmpty())
        return tr("Default: empty");
    if (text.size() > 64) {
        text = text.left(61) + QStringLiteral("...");
        return tr("Default: %1").arg(text);
    }
    return tr("Default: %1").arg(text);
}
