#pragma once

#include <algorithm>
#include <iterator>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QList>
#include <QLocale>
#include <QSet>
#include <QString>
#include <QVector>

struct LangItem
{
    QString code;
    // Prompt text handed to the model, and the source string of the name shown
    // in the UI. A literal so the UI can pass it to QCoreApplication::translate.
    const char* en;
};

// A user-defined language: the key goes into the prompt and the settings; the
// name labels the pickers.
struct CustomLanguage
{
    QString key;
    QString name;
};

namespace Languages {

inline const QVector<LangItem>& all()
{
    static const QVector<LangItem> list = {
        {QStringLiteral("auto"), QT_TRANSLATE_NOOP("Languages", "Auto detect")},
        {QStringLiteral("zh"), QT_TRANSLATE_NOOP("Languages", "Chinese")},
        {QStringLiteral("en"), QT_TRANSLATE_NOOP("Languages", "English")},
        {QStringLiteral("ar"), QT_TRANSLATE_NOOP("Languages", "Arabic")},
        {QStringLiteral("bn"), QT_TRANSLATE_NOOP("Languages", "Bengali")},
        {QStringLiteral("my"), QT_TRANSLATE_NOOP("Languages", "Burmese")},
        {QStringLiteral("yue"), QT_TRANSLATE_NOOP("Languages", "Cantonese")},
        {QStringLiteral("cs"), QT_TRANSLATE_NOOP("Languages", "Czech")},
        {QStringLiteral("nl"), QT_TRANSLATE_NOOP("Languages", "Dutch")},
        {QStringLiteral("fil"), QT_TRANSLATE_NOOP("Languages", "Filipino")},
        {QStringLiteral("fr"), QT_TRANSLATE_NOOP("Languages", "French")},
        {QStringLiteral("de"), QT_TRANSLATE_NOOP("Languages", "German")},
        {QStringLiteral("el"), QT_TRANSLATE_NOOP("Languages", "Greek")},
        {QStringLiteral("gu"), QT_TRANSLATE_NOOP("Languages", "Gujarati")},
        {QStringLiteral("he"), QT_TRANSLATE_NOOP("Languages", "Hebrew")},
        {QStringLiteral("hi"), QT_TRANSLATE_NOOP("Languages", "Hindi")},
        {QStringLiteral("id"), QT_TRANSLATE_NOOP("Languages", "Indonesian")},
        {QStringLiteral("it"), QT_TRANSLATE_NOOP("Languages", "Italian")},
        {QStringLiteral("ja"), QT_TRANSLATE_NOOP("Languages", "Japanese")},
        {QStringLiteral("kk"), QT_TRANSLATE_NOOP("Languages", "Kazakh")},
        {QStringLiteral("km"), QT_TRANSLATE_NOOP("Languages", "Khmer")},
        {QStringLiteral("ko"), QT_TRANSLATE_NOOP("Languages", "Korean")},
        {QStringLiteral("ms"), QT_TRANSLATE_NOOP("Languages", "Malay")},
        {QStringLiteral("mr"), QT_TRANSLATE_NOOP("Languages", "Marathi")},
        {QStringLiteral("mn"), QT_TRANSLATE_NOOP("Languages", "Mongolian")},
        {QStringLiteral("fa"), QT_TRANSLATE_NOOP("Languages", "Persian")},
        {QStringLiteral("pl"), QT_TRANSLATE_NOOP("Languages", "Polish")},
        {QStringLiteral("pt"), QT_TRANSLATE_NOOP("Languages", "Portuguese")},
        {QStringLiteral("ru"), QT_TRANSLATE_NOOP("Languages", "Russian")},
        {QStringLiteral("es"), QT_TRANSLATE_NOOP("Languages", "Spanish")},
        {QStringLiteral("ta"), QT_TRANSLATE_NOOP("Languages", "Tamil")},
        {QStringLiteral("te"), QT_TRANSLATE_NOOP("Languages", "Telugu")},
        {QStringLiteral("th"), QT_TRANSLATE_NOOP("Languages", "Thai")},
        {QStringLiteral("bo"), QT_TRANSLATE_NOOP("Languages", "Tibetan")},
        {QStringLiteral("zh-Hant"), QT_TRANSLATE_NOOP("Languages", "Traditional Chinese")},
        {QStringLiteral("tr"), QT_TRANSLATE_NOOP("Languages", "Turkish")},
        {QStringLiteral("uk"), QT_TRANSLATE_NOOP("Languages", "Ukrainian")},
        {QStringLiteral("ur"), QT_TRANSLATE_NOOP("Languages", "Urdu")},
        {QStringLiteral("ug"), QT_TRANSLATE_NOOP("Languages", "Uyghur")},
        {QStringLiteral("vi"), QT_TRANSLATE_NOOP("Languages", "Vietnamese")}
    };
    return list;
}

inline int indexOf(const QString& code)
{
    const QVector<LangItem>& list = all();
    const auto found = std::find_if(list.cbegin(), list.cend(),
                                    [&code](const LangItem& lang) {
                                        return lang.code == code;
                                    });
    return found == list.cend() ? -1
                                : static_cast<int>(std::distance(list.cbegin(), found));
}

// Completes a half-filled entry so a picker can offer it, and drops one whose
// key a built-in language or an earlier entry already uses.
inline QVector<CustomLanguage> customNormalized(const QVector<CustomLanguage>& custom)
{
    QVector<CustomLanguage> result;
    result.reserve(custom.size());
    QSet<QString> keys;
    for (const CustomLanguage& stored : custom) {
        CustomLanguage language;
        language.key = stored.key.trimmed();
        language.name = stored.name.trimmed();
        if (language.name.isEmpty())
            language.name = language.key;
        else if (language.key.isEmpty())
            language.key = language.name;
        if (language.key.isEmpty() || indexOf(language.key) >= 0 || keys.contains(language.key))
            continue;
        keys.insert(language.key);
        result.append(language);
    }
    return result;
}

inline QVector<CustomLanguage> customFromJson(const QJsonArray& stored)
{
    QVector<CustomLanguage> custom;
    custom.reserve(stored.size());
    for (const QJsonValue& value : stored) {
        const QJsonObject object = value.toObject();
        custom.append({object.value(QStringLiteral("key")).toString(),
                       object.value(QStringLiteral("name")).toString()});
    }
    return customNormalized(custom);
}

inline QJsonArray customToJson(const QVector<CustomLanguage>& custom)
{
    QJsonArray array;
    for (const CustomLanguage& language : custom) {
        QJsonObject object;
        object.insert(QStringLiteral("key"), language.key);
        object.insert(QStringLiteral("name"), language.name);
        array.append(object);
    }
    return array;
}

// Prompt text for \p selected; a custom key passes through unchanged.
inline QString englishName(const QString& selected)
{
    if (selected == QLatin1String("auto"))
        return QStringLiteral("the detected language");
    const int index = indexOf(selected);
    return index >= 0 ? QString::fromUtf8(all().at(index).en) : selected;
}

// Untranslated name for the UI, or nullptr for an unknown code; the caller
// resolves it through the "Languages" catalog.
inline const char* labelFor(const QString& code)
{
    const int index = indexOf(code);
    return index >= 0 ? all().at(index).en : nullptr;
}

// Returns the code of the dominant Unicode script in \p text among the
// supported languages, or an empty string when the text contains none of those
// scripts. Only code points that carry a script of their own count: spaces,
// punctuation and combining marks report Common or Inherited (an Arabic comma,
// a Devanagari danda, the katakana prolonged sound mark), so text made up
// solely of them yields no language. Latin-script languages share one alphabet
// and cannot be told apart here. A tie goes to the language listed first.
inline QString guessFromScript(const QString& text)
{
    enum Language : int {
        Arabic, Bengali, Tibetan, Greek, Gujarati, Hebrew, Hindi, Japanese,
        Khmer, Korean, Mongolian, Burmese, Russian, Tamil, Telugu, Thai,
        Chinese, LanguageCount
    };
    static constexpr QLocale::Language kLanguages[LanguageCount] = {
        QLocale::Arabic, QLocale::Bengali, QLocale::Tibetan, QLocale::Greek,
        QLocale::Gujarati, QLocale::Hebrew, QLocale::Hindi, QLocale::Japanese,
        QLocale::Khmer, QLocale::Korean, QLocale::Mongolian, QLocale::Burmese,
        QLocale::Russian, QLocale::Tamil, QLocale::Telugu, QLocale::Thai,
        QLocale::Chinese
    };

    int counts[LanguageCount] = {};
    const QList<uint> codePoints = text.toUcs4();
    for (uint codePoint : codePoints) {
        switch (QChar::script(codePoint)) {
        case QChar::Script_Arabic: ++counts[Arabic]; break;
        case QChar::Script_Bengali: ++counts[Bengali]; break;
        case QChar::Script_Tibetan: ++counts[Tibetan]; break;
        case QChar::Script_Greek: ++counts[Greek]; break;
        case QChar::Script_Gujarati: ++counts[Gujarati]; break;
        case QChar::Script_Hebrew: ++counts[Hebrew]; break;
        case QChar::Script_Devanagari: ++counts[Hindi]; break;
        case QChar::Script_Hiragana:
        case QChar::Script_Katakana: ++counts[Japanese]; break;
        case QChar::Script_Khmer: ++counts[Khmer]; break;
        case QChar::Script_Hangul: ++counts[Korean]; break;
        case QChar::Script_Mongolian: ++counts[Mongolian]; break;
        case QChar::Script_Myanmar: ++counts[Burmese]; break;
        case QChar::Script_Cyrillic: ++counts[Russian]; break;
        case QChar::Script_Tamil: ++counts[Tamil]; break;
        case QChar::Script_Telugu: ++counts[Telugu]; break;
        case QChar::Script_Thai: ++counts[Thai]; break;
        case QChar::Script_Han: ++counts[Chinese]; break;
        default: break;
        }
    }

    int best = -1;
    for (int i = 0; i < LanguageCount; ++i) {
        if (counts[i] > 0 && (best < 0 || counts[i] > counts[best]))
            best = i;
    }
    return best < 0 ? QString() : QLocale::languageToCode(kLanguages[best]);
}

// Resolves the "auto" pseudo-language into a concrete code for operations that
// require two distinct languages: prefers the script detected from \p text and
// otherwise falls back to the first supported non-auto code differing from
// \p exclude.
inline QString resolveAuto(const QString& text, const QString& exclude)
{
    const QString detected = guessFromScript(text);
    if (!detected.isEmpty() && detected != exclude)
        return detected;
    for (const LangItem& lang : all()) {
        if (lang.code != QLatin1String("auto") && lang.code != exclude)
            return lang.code;
    }
    // Unreachable while the language list holds more than one concrete code.
    return detected;
}

}
