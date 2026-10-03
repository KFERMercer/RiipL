#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

class QJsonObject;

struct TranslationRecord
{
    qint64 timestamp = 0;
    QString sourceLang;
    QString targetLang;
    QString source;
    QString target;
    QString tone;

    bool isValid() const { return !source.isEmpty() && !target.isEmpty(); }

    bool operator==(const TranslationRecord& other) const
    {
        return timestamp == other.timestamp && sourceLang == other.sourceLang
            && targetLang == other.targetLang && source == other.source && target == other.target
            && tone == other.tone;
    }
};

class HistoryManager : public QObject
{
    Q_OBJECT

public:
    explicit HistoryManager(const QString& filePath, QObject* parent = nullptr);
    ~HistoryManager() override;

    QVector<TranslationRecord> records() const { return m_records; }
    void addRecord(const TranslationRecord& record);
    void removeRecord(int index);
    void clear();
    void setMaxRecords(int maxRecords);
    void flush();

signals:
    void changed();

private:
    void load();
    void save();
    void scheduleSave();
    void trim();

    static QJsonObject toJson(const TranslationRecord& record);
    static TranslationRecord fromJson(const QJsonObject& object);

    QString m_filePath;
    QVector<TranslationRecord> m_records;
    int m_maxRecords = 500;
    QTimer m_saveTimer;
};
