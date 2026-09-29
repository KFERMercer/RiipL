#pragma once

#include <QString>
#include <QVector>

struct ToneItem
{
    QString key;
    // A literal so the UI can pass it to QCoreApplication::translate.
    const char* en;
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

}
