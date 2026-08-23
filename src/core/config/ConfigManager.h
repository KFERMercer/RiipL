#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

class ConfigManager : public QObject
{
    Q_OBJECT

public:
    static void createInstance(const QString& configDir = QString());
    static ConfigManager* instance();

    QJsonValue value(const QString& key) const;
    QString stringValue(const QString& key) const;
    QString resolvedUiLanguage() const;
    bool boolValue(const QString& key) const;
    int intValue(const QString& key) const;
    double doubleValue(const QString& key) const;

    bool isDefault(const QString& key) const;
    void setValue(const QString& key, const QJsonValue& value);
    void removeValue(const QString& key);

    QJsonObject userDocument() const { return m_user; }
    void resetTo(const QJsonObject& doc);
    void flush();

    QString configDir() const { return m_dir; }
    QString configFilePath() const;
    QString historyFilePath() const;

signals:
    void changed(const QString& key);

private:
    explicit ConfigManager(const QString& configDir);
    void importLegacyFiles();
    void load();
    void scheduleSave();
    void save();

    static ConfigManager* s_instance;
    QJsonObject m_user;
    QString m_dir;
    QTimer m_saveTimer;
};
