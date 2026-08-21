#include "HistoryManager.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QTimer>

HistoryManager::HistoryManager(const QString& filePath, QObject* parent)
    : QObject(parent)
    , m_filePath(filePath)
{
    load();
}

void HistoryManager::load()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray())
        return;
    m_records.clear();
    const QJsonArray array = doc.array();
    for (const QJsonValue& value : array) {
        const QJsonObject object = value.toObject();
        TranslationRecord record;
        record.timestamp = static_cast<qint64>(object.value(QStringLiteral("timestamp")).toDouble());
        record.sourceLang = object.value(QStringLiteral("source_lang")).toString();
        record.targetLang = object.value(QStringLiteral("target_lang")).toString();
        record.source = object.value(QStringLiteral("source")).toString();
        record.target = object.value(QStringLiteral("target")).toString();
        record.tone = object.value(QStringLiteral("tone")).toString();
        if (record.isValid())
            m_records.append(record);
    }
}

void HistoryManager::save()
{
    QJsonArray array;
    for (const TranslationRecord& record : std::as_const(m_records)) {
        QJsonObject object;
        object.insert(QStringLiteral("timestamp"), static_cast<double>(record.timestamp));
        object.insert(QStringLiteral("source_lang"), record.sourceLang);
        object.insert(QStringLiteral("target_lang"), record.targetLang);
        object.insert(QStringLiteral("source"), record.source);
        object.insert(QStringLiteral("target"), record.target);
        object.insert(QStringLiteral("tone"), record.tone);
        array.append(object);
    }
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    file.commit();
}

void HistoryManager::trim()
{
    while (m_records.size() > m_maxRecords)
        m_records.removeLast();
}

void HistoryManager::addRecord(const TranslationRecord& record)
{
    if (!record.isValid() || m_maxRecords <= 0)
        return;
    m_records.prepend(record);
    trim();
    save();
    emit changed();
}

void HistoryManager::removeRecord(int index)
{
    if (index < 0 || index >= m_records.size())
        return;
    m_records.removeAt(index);
    save();
    emit changed();
}

void HistoryManager::clear()
{
    m_records.clear();
    save();
    emit changed();
}

void HistoryManager::setMaxRecords(int maxRecords)
{
    m_maxRecords = maxRecords;
    trim();
}
