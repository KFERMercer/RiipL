#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

// A named snapshot of the API settings, so a user can keep several providers
// and switch between them without retyping the endpoint, key and model.
struct ApiPreset
{
    QString name;
    QJsonObject values;
};

namespace ApiPresets {

// Fills in every captured field, so two presets can be compared as plain JSON
// objects. A field a preset does not carry takes its default, which keeps
// presets saved before a field existed comparable against the current one.
inline QJsonObject withDefaults(const QJsonObject& values)
{
    QJsonObject result;
    for (const QString& key : Keys::apiPresetFields()) {
        const QJsonValue stored = values.value(key);
        result.insert(key, stored.isUndefined() ? Defaults::value(key) : stored);
    }
    return result;
}

// The API fields as currently applied in the configuration.
inline QJsonObject capture()
{
    QJsonObject values;
    ConfigManager* config = ConfigManager::instance();
    for (const QString& key : Keys::apiPresetFields())
        values.insert(key, config->value(key));
    return values;
}

// Writes a preset's fields into the running configuration.
inline void apply(const ApiPreset& preset)
{
    const QJsonObject values = withDefaults(preset.values);
    ConfigManager* config = ConfigManager::instance();
    for (const QString& key : Keys::apiPresetFields())
        config->setValue(key, values.value(key));
}

inline QJsonArray toJson(const QVector<ApiPreset>& presets)
{
    QJsonArray array;
    for (const ApiPreset& preset : presets) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), preset.name);
        object.insert(QStringLiteral("values"), preset.values);
        array.append(object);
    }
    return array;
}

inline QVector<ApiPreset> fromJson(const QJsonArray& stored)
{
    QVector<ApiPreset> presets;
    presets.reserve(stored.size());
    for (const QJsonValue& entry : stored) {
        const QJsonObject object = entry.toObject();
        ApiPreset preset;
        preset.name = object.value(QStringLiteral("name")).toString().trimmed();
        preset.values = object.value(QStringLiteral("values")).toObject();
        if (!preset.name.isEmpty())
            presets.append(preset);
    }
    return presets;
}

// Index of the first preset named \p name, or -1.
inline int indexOf(const QVector<ApiPreset>& presets, const QString& name)
{
    for (qsizetype i = 0; i < presets.size(); ++i) {
        if (presets.at(i).name == name)
            return int(i);
    }
    return -1;
}

// Index of the preset matching \p values, or -1 when the settings have drifted
// away from every stored preset.
inline int matchValues(const QVector<ApiPreset>& presets, const QJsonObject& values)
{
    const QJsonObject current = withDefaults(values);
    for (qsizetype i = 0; i < presets.size(); ++i) {
        if (withDefaults(presets.at(i).values) == current)
            return int(i);
    }
    return -1;
}

}