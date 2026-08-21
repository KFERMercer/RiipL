#include "Glossary.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

QJsonArray Glossary::toJson(const QVector<GlossaryEntry>& entries)
{
    QJsonArray array;
    for (const GlossaryEntry& entry : entries) {
        QJsonObject object;
        object.insert(QStringLiteral("source"), entry.source);
        object.insert(QStringLiteral("target"), entry.target);
        array.append(object);
    }
    return array;
}

QVector<GlossaryEntry> Glossary::fromJson(const QJsonArray& array)
{
    QVector<GlossaryEntry> result;
    result.reserve(array.size());
    for (const QJsonValue& value : array) {
        const QJsonObject object = value.toObject();
        GlossaryEntry entry;
        entry.source = object.value(QStringLiteral("source")).toString();
        entry.target = object.value(QStringLiteral("target")).toString();
        if (!entry.source.isEmpty())
            result.append(entry);
    }
    return result;
}

void Glossary::saveToConfig() const
{
    if (ConfigManager* config = ConfigManager::instance())
        config->setValue(Keys::glossaryEntries, toJson(entries));
}

Glossary Glossary::loadFromConfig()
{
    Glossary glossary;
    if (const ConfigManager* config = ConfigManager::instance())
        glossary.entries = fromJson(config->value(Keys::glossaryEntries).toArray());
    return glossary;
}

bool Glossary::isEnabled() const
{
    const ConfigManager* config = ConfigManager::instance();
    return !config || config->boolValue(Keys::glossaryEnabled);
}
