#include "HistoryManager.h"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

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

// One record per line, oldest first, so a new record costs one append.
void HistoryManager::load()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray data = file.readAll();
    file.close();

    m_records.clear();
    QVector<TranslationRecord> oldestFirst;
    // A line a crash cut short fails to parse and is skipped.
    for (const QByteArray& line : data.split('\n')) {
        if (line.trimmed().isEmpty())
            continue;
        const TranslationRecord record = fromJson(QJsonDocument::fromJson(line).object());
        if (record.isValid())
            oldestFirst.append(record);
    }
    m_records.reserve(oldestFirst.size());
    for (auto it = oldestFirst.crbegin(); it != oldestFirst.crend(); ++it)
        m_records.append(*it);
    m_fileRecords = m_records.size();
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
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "RiipL: cannot write history file" << file.fileName() << file.errorString();
        return;
    }
    QByteArray data;
    for (auto it = m_records.crbegin(); it != m_records.crend(); ++it)
        data += QJsonDocument(toJson(*it)).toJson(QJsonDocument::Compact) + '\n';
    if (file.write(data) != data.size()) {
        qWarning() << "RiipL: cannot write history file" << file.fileName() << file.errorString();
        return;
    }
    if (!file.commit()) {
        qWarning() << "RiipL: cannot commit history file" << file.fileName() << file.errorString();
        return;
    }
    m_fileRecords = m_records.size();
}

void HistoryManager::append(const TranslationRecord& record)
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        qWarning() << "RiipL: cannot append to history file" << file.fileName()
                   << file.errorString();
        return;
    }
    const QByteArray line = QJsonDocument(toJson(record)).toJson(QJsonDocument::Compact) + '\n';
    if (file.write(line) != line.size()) {
        qWarning() << "RiipL: cannot append to history file" << file.fileName()
                   << file.errorString();
        return;
    }
    ++m_fileRecords;
}

bool HistoryManager::shouldCompact() const
{
    return m_fileRecords * 8 > m_maxRecords * 9;
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
    append(record);
    if (shouldCompact())
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
