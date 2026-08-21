#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

struct GlossaryEntry
{
    QString source;
    QString target;

    bool operator==(const GlossaryEntry& other) const
    {
        return source == other.source && target == other.target;
    }
};

class Glossary
{
public:
    QVector<GlossaryEntry> entries;

    static QJsonArray toJson(const QVector<GlossaryEntry>& entries);
    static QVector<GlossaryEntry> fromJson(const QJsonArray& array);

    void saveToConfig() const;
    static Glossary loadFromConfig();
    bool isEnabled() const;
};
