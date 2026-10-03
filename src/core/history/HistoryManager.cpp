#include "HistoryManager.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <utility>

HistoryManager::HistoryManager(const QString& filePath, QObject* parent)
    : QObject(parent)
    , m_filePath(filePath)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &HistoryManager::save);
    load();
}

HistoryManager::~HistoryManager()
{
    flush();
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
        const TranslationRecord record = fromJson(value.toObject());
        if (record.isValid())
            m_records.append(record);
    }
}

void HistoryManager::scheduleSave()
{
    m_saveTimer.start();
}

void HistoryManager::flush()
{
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        save();
    }
}

void HistoryManager::save()
{
    QJsonArray array;
    for (const TranslationRecord& record : std::as_const(m_records))
        array.append(toJson(record));
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "RiipL: cannot write history file" << file.fileName() << file.errorString();
        return;
    }
    const QByteArray data = QJsonDocument(array).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        qWarning() << "RiipL: cannot write history file" << file.fileName() << file.errorString();
        return;
    }
    if (!file.commit())
        qWarning() << "RiipL: cannot commit history file" << file.fileName() << file.errorString();
}

QJsonObject HistoryManager::toJson(const TranslationRecord& record)
{
    QJsonObject object;
    object.insert(QStringLiteral("timestamp"), static_cast<double>(record.timestamp));
    object.insert(QStringLiteral("source_lang"), record.sourceLang);
    object.insert(QStringLiteral("target_lang"), record.targetLang);
    object.insert(QStringLiteral("source"), record.source);
    object.insert(QStringLiteral("target"), record.target);
    object.insert(QStringLiteral("tone"), record.tone);
    return object;
}

TranslationRecord HistoryManager::fromJson(const QJsonObject& object)
{
    TranslationRecord record;
    record.timestamp = static_cast<qint64>(object.value(QStringLiteral("timestamp")).toDouble());
    record.sourceLang = object.value(QStringLiteral("source_lang")).toString();
    record.targetLang = object.value(QStringLiteral("target_lang")).toString();
    record.source = object.value(QStringLiteral("source")).toString();
    record.target = object.value(QStringLiteral("target")).toString();
    record.tone = object.value(QStringLiteral("tone")).toString();
    return record;
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
    scheduleSave();
    emit changed();
}

void HistoryManager::removeRecord(int index)
{
    if (index < 0 || index >= m_records.size())
        return;
    m_records.removeAt(index);
    scheduleSave();
    emit changed();
}

void HistoryManager::clear()
{
    m_records.clear();
    scheduleSave();
    emit changed();
}

void HistoryManager::setMaxRecords(int maxRecords)
{
    m_maxRecords = maxRecords;
    const int before = m_records.size();
    trim();
    if (m_records.size() == before)
        return;
    scheduleSave();
    emit changed();
}
