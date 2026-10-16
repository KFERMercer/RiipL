#pragma once

#include <algorithm>
#include <iterator>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

// A named snapshot of one group of configuration fields, so a user can keep
// several variants and switch between them without retyping the values.
struct Preset
{
    QString name;
    QJsonObject values;
};

namespace Presets {

// Fills in every captured field, so two presets compare as plain JSON objects; a
// field a preset does not carry takes its default, keeping older presets comparable.
inline QJsonObject withDefaults(const QJsonObject& values, const QStringList& fields)
{
    QJsonObject result;
    for (const QString& key : fields) {
        const QJsonValue stored = values.value(key);
        result.insert(key, stored.isUndefined() ? Defaults::value(key) : stored);
    }
    return result;
}

// The fields as currently applied in the configuration.
inline QJsonObject capture(const QStringList& fields)
{
    QJsonObject values;
    ConfigManager* config = ConfigManager::instance();
    for (const QString& key : fields)
        values.insert(key, config->value(key));
    return values;
}

// Writes a preset's fields into the running configuration.
inline void apply(const Preset& preset, const QStringList& fields)
{
    const QJsonObject values = withDefaults(preset.values, fields);
    ConfigManager* config = ConfigManager::instance();
    for (const QString& key : fields)
        config->setValue(key, values.value(key));
}

inline QJsonArray toJson(const QVector<Preset>& presets)
{
    QJsonArray array;
    for (const Preset& preset : presets) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), preset.name);
        object.insert(QStringLiteral("values"), preset.values);
        array.append(object);
    }
    return array;
}

inline QVector<Preset> fromJson(const QJsonArray& stored)
{
    QVector<Preset> presets;
    presets.reserve(stored.size());
    for (const QJsonValue& entry : stored) {
        const QJsonObject object = entry.toObject();
        Preset preset;
        preset.name = object.value(QStringLiteral("name")).toString().trimmed();
        preset.values = object.value(QStringLiteral("values")).toObject();
        if (!preset.name.isEmpty())
            presets.append(preset);
    }
    return presets;
}

// Index of the first preset named \p name, or -1.
inline int indexOf(const QVector<Preset>& presets, const QString& name)
{
    const auto found = std::find_if(presets.cbegin(), presets.cend(),
                                    [&name](const Preset& preset) {
                                        return preset.name == name;
                                    });
    return found == presets.cend()
        ? -1
        : static_cast<int>(std::distance(presets.cbegin(), found));
}

// Index of the preset matching \p values, or -1 when the fields have drifted
// away from every stored preset.
inline int matchValues(const QVector<Preset>& presets, const QJsonObject& values,
                       const QStringList& fields)
{
    const QJsonObject current = withDefaults(values, fields);
    for (qsizetype i = 0; i < presets.size(); ++i) {
        if (withDefaults(presets.at(i).values, fields) == current)
            return int(i);
    }
    return -1;
}

}
