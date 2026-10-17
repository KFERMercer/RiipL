#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>

struct ToneItem
{
    QString key;
    // A literal so the UI can pass it to QCoreApplication::translate.
    const char* en;
};

// A user-defined tone: the key goes into the prompt and the settings; the name
// labels the pickers.
struct CustomTone
{
    QString key;
    QString name;
};

namespace Tones {

inline const QVector<ToneItem>& presets()
{
    static const QVector<ToneItem> list = {
        {QStringLiteral("default"), QT_TRANSLATE_NOOP("Tones", "Default")},
        {QStringLiteral("formal"), QT_TRANSLATE_NOOP("Tones", "Formal")},
        {QStringLiteral("casual"), QT_TRANSLATE_NOOP("Tones", "Casual")},
        {QStringLiteral("neutral"), QT_TRANSLATE_NOOP("Tones", "Neutral")},
        {QStringLiteral("technical"), QT_TRANSLATE_NOOP("Tones", "Technical")},
        {QStringLiteral("marketing"), QT_TRANSLATE_NOOP("Tones", "Marketing")},
        {QStringLiteral("literary"), QT_TRANSLATE_NOOP("Tones", "Literary")},
        {QStringLiteral("academic"), QT_TRANSLATE_NOOP("Tones", "Academic")},
        {QStringLiteral("legal"), QT_TRANSLATE_NOOP("Tones", "Legal")},
        {QStringLiteral("literal"), QT_TRANSLATE_NOOP("Tones", "Literal")},
        {QStringLiteral("idiomatic"), QT_TRANSLATE_NOOP("Tones", "Idiomatic")},
        {QStringLiteral("transcreation"), QT_TRANSLATE_NOOP("Tones", "Transcreation")},
        {QStringLiteral("machine-like"), QT_TRANSLATE_NOOP("Tones", "Machine-like")},
        {QStringLiteral("concise"), QT_TRANSLATE_NOOP("Tones", "Concise")}
    };
    return list;
}

// Untranslated name for the UI, or nullptr for a custom key; the caller resolves
// it through the "Tones" catalog.
inline const char* labelFor(const QString& key)
{
    for (const ToneItem& item : presets()) {
        if (item.key == key)
            return item.en;
    }
    return nullptr;
}

// Drops the entries no picker could offer: a tone carries both the key the
// prompts name and the name it shows.
inline QVector<CustomTone> customNormalized(const QVector<CustomTone>& custom)
{
    QVector<CustomTone> result;
    result.reserve(custom.size());
    for (const CustomTone& stored : custom) {
        CustomTone tone;
        tone.key = stored.key.trimmed();
        tone.name = stored.name.trimmed();
        if (tone.key.isEmpty() || tone.name.isEmpty())
            continue;
        result.append(tone);
    }
    return result;
}

inline QVector<CustomTone> customFromJson(const QJsonArray& stored)
{
    QVector<CustomTone> custom;
    custom.reserve(stored.size());
    for (const QJsonValue& value : stored) {
        const QJsonObject object = value.toObject();
        custom.append({object.value(QStringLiteral("key")).toString(),
                       object.value(QStringLiteral("name")).toString()});
    }
    return customNormalized(custom);
}

inline QJsonArray customToJson(const QVector<CustomTone>& custom)
{
    QJsonArray array;
    for (const CustomTone& tone : custom) {
        QJsonObject object;
        object.insert(QStringLiteral("key"), tone.key);
        object.insert(QStringLiteral("name"), tone.name);
        array.append(object);
    }
    return array;
}

}
