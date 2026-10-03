#pragma once

#include <algorithm>
#include <iterator>
#include <QMap>
#include <QString>
#include <QVector>

struct LangItem
{
    QString code;
    // Prompt text handed to the model, and the source string of the name shown
    // in the UI. A literal so the UI can pass it to QCoreApplication::translate.
    const char* en;
};

namespace Languages {

inline const QVector<LangItem>& all()
{
    static const QVector<LangItem> list = {
        {QStringLiteral("auto"), QT_TRANSLATE_NOOP("Languages", "Auto detect")},
        {QStringLiteral("zh"), QT_TRANSLATE_NOOP("Languages", "Chinese")},
        {QStringLiteral("en"), QT_TRANSLATE_NOOP("Languages", "English")},
        {QStringLiteral("fr"), QT_TRANSLATE_NOOP("Languages", "French")},
        {QStringLiteral("pt"), QT_TRANSLATE_NOOP("Languages", "Portuguese")},
        {QStringLiteral("es"), QT_TRANSLATE_NOOP("Languages", "Spanish")},
        {QStringLiteral("ja"), QT_TRANSLATE_NOOP("Languages", "Japanese")},
        {QStringLiteral("tr"), QT_TRANSLATE_NOOP("Languages", "Turkish")},
        {QStringLiteral("ru"), QT_TRANSLATE_NOOP("Languages", "Russian")},
        {QStringLiteral("ar"), QT_TRANSLATE_NOOP("Languages", "Arabic")},
        {QStringLiteral("ko"), QT_TRANSLATE_NOOP("Languages", "Korean")},
        {QStringLiteral("th"), QT_TRANSLATE_NOOP("Languages", "Thai")},
        {QStringLiteral("it"), QT_TRANSLATE_NOOP("Languages", "Italian")},
        {QStringLiteral("de"), QT_TRANSLATE_NOOP("Languages", "German")},
        {QStringLiteral("vi"), QT_TRANSLATE_NOOP("Languages", "Vietnamese")},
        {QStringLiteral("ms"), QT_TRANSLATE_NOOP("Languages", "Malay")},
        {QStringLiteral("id"), QT_TRANSLATE_NOOP("Languages", "Indonesian")},
        {QStringLiteral("fil"), QT_TRANSLATE_NOOP("Languages", "Filipino")},
        {QStringLiteral("hi"), QT_TRANSLATE_NOOP("Languages", "Hindi")},
        {QStringLiteral("zh-Hant"), QT_TRANSLATE_NOOP("Languages", "Traditional Chinese")},
        {QStringLiteral("pl"), QT_TRANSLATE_NOOP("Languages", "Polish")},
        {QStringLiteral("cs"), QT_TRANSLATE_NOOP("Languages", "Czech")},
        {QStringLiteral("nl"), QT_TRANSLATE_NOOP("Languages", "Dutch")},
        {QStringLiteral("km"), QT_TRANSLATE_NOOP("Languages", "Khmer")},
        {QStringLiteral("my"), QT_TRANSLATE_NOOP("Languages", "Burmese")},
        {QStringLiteral("fa"), QT_TRANSLATE_NOOP("Languages", "Persian")},
        {QStringLiteral("gu"), QT_TRANSLATE_NOOP("Languages", "Gujarati")},
        {QStringLiteral("ur"), QT_TRANSLATE_NOOP("Languages", "Urdu")},
        {QStringLiteral("te"), QT_TRANSLATE_NOOP("Languages", "Telugu")},
        {QStringLiteral("mr"), QT_TRANSLATE_NOOP("Languages", "Marathi")},
        {QStringLiteral("he"), QT_TRANSLATE_NOOP("Languages", "Hebrew")},
        {QStringLiteral("bn"), QT_TRANSLATE_NOOP("Languages", "Bengali")},
        {QStringLiteral("ta"), QT_TRANSLATE_NOOP("Languages", "Tamil")},
        {QStringLiteral("uk"), QT_TRANSLATE_NOOP("Languages", "Ukrainian")},
        {QStringLiteral("bo"), QT_TRANSLATE_NOOP("Languages", "Tibetan")},
        {QStringLiteral("kk"), QT_TRANSLATE_NOOP("Languages", "Kazakh")},
        {QStringLiteral("mn"), QT_TRANSLATE_NOOP("Languages", "Mongolian")},
        {QStringLiteral("ug"), QT_TRANSLATE_NOOP("Languages", "Uyghur")},
        {QStringLiteral("yue"), QT_TRANSLATE_NOOP("Languages", "Cantonese")}
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

inline QString englishName(const QString& code)
{
    if (code == QLatin1String("auto"))
        return QStringLiteral("the detected language");
    const int index = indexOf(code);
    return index >= 0 ? QString::fromUtf8(all().at(index).en) : code;
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
// and cannot be told apart here.
inline QString guessFromScript(const QString& text)
{
    QMap<QString, int> counts;
    const QList<uint> codePoints = text.toUcs4();
    for (uint codePoint : codePoints) {
        switch (QChar::script(codePoint)) {
        case QChar::Script_Cyrillic: ++counts[QStringLiteral("ru")]; break;
        case QChar::Script_Hebrew: ++counts[QStringLiteral("he")]; break;
        case QChar::Script_Arabic: ++counts[QStringLiteral("ar")]; break;
        case QChar::Script_Devanagari: ++counts[QStringLiteral("hi")]; break;
        case QChar::Script_Bengali: ++counts[QStringLiteral("bn")]; break;
        case QChar::Script_Gujarati: ++counts[QStringLiteral("gu")]; break;
        case QChar::Script_Tamil: ++counts[QStringLiteral("ta")]; break;
        case QChar::Script_Telugu: ++counts[QStringLiteral("te")]; break;
        case QChar::Script_Thai: ++counts[QStringLiteral("th")]; break;
        case QChar::Script_Tibetan: ++counts[QStringLiteral("bo")]; break;
        case QChar::Script_Myanmar: ++counts[QStringLiteral("my")]; break;
        case QChar::Script_Hangul: ++counts[QStringLiteral("ko")]; break;
        case QChar::Script_Khmer: ++counts[QStringLiteral("km")]; break;
        case QChar::Script_Mongolian: ++counts[QStringLiteral("mn")]; break;
        case QChar::Script_Hiragana:
        case QChar::Script_Katakana: ++counts[QStringLiteral("ja")]; break;
        case QChar::Script_Han: ++counts[QStringLiteral("zh")]; break;
        default: break;
        }
    }

    QString best;
    int bestCount = 0;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() > bestCount) {
            best = it.key();
            bestCount = it.value();
        }
    }
    return best;
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
