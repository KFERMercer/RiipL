#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include "core/models/Glossary.h"

struct TranslationRecord
{
    qint64 timestamp = 0;
    QString sourceLang;
    QString targetLang;
    QString source;
    QString target;
    QString tone;

    bool isValid() const { return !source.isEmpty() && !target.isEmpty(); }
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

    QString m_filePath;
    QVector<TranslationRecord> m_records;
    int m_maxRecords = 500;
    QTimer m_saveTimer;
};
