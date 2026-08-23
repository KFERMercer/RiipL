#pragma once

#include <QMap>
#include <QString>
#include <QVector>

struct LangItem
{
    QString code;
    QString en;
    QString zh;
};

namespace Languages {

inline const QVector<LangItem>& all()
{
    static const QVector<LangItem> list = {
        {QStringLiteral("auto"), QStringLiteral("Auto detect"), QStringLiteral("自动检测")},
        {QStringLiteral("zh"), QStringLiteral("Chinese"), QStringLiteral("中文")},
        {QStringLiteral("en"), QStringLiteral("English"), QStringLiteral("英语")},
        {QStringLiteral("fr"), QStringLiteral("French"), QStringLiteral("法语")},
        {QStringLiteral("pt"), QStringLiteral("Portuguese"), QStringLiteral("葡萄牙语")},
        {QStringLiteral("es"), QStringLiteral("Spanish"), QStringLiteral("西班牙语")},
        {QStringLiteral("ja"), QStringLiteral("Japanese"), QStringLiteral("日语")},
        {QStringLiteral("tr"), QStringLiteral("Turkish"), QStringLiteral("土耳其语")},
        {QStringLiteral("ru"), QStringLiteral("Russian"), QStringLiteral("俄语")},
        {QStringLiteral("ar"), QStringLiteral("Arabic"), QStringLiteral("阿拉伯语")},
        {QStringLiteral("ko"), QStringLiteral("Korean"), QStringLiteral("韩语")},
        {QStringLiteral("th"), QStringLiteral("Thai"), QStringLiteral("泰语")},
        {QStringLiteral("it"), QStringLiteral("Italian"), QStringLiteral("意大利语")},
        {QStringLiteral("de"), QStringLiteral("German"), QStringLiteral("德语")},
        {QStringLiteral("vi"), QStringLiteral("Vietnamese"), QStringLiteral("越南语")},
        {QStringLiteral("ms"), QStringLiteral("Malay"), QStringLiteral("马来语")},
        {QStringLiteral("id"), QStringLiteral("Indonesian"), QStringLiteral("印尼语")},
        {QStringLiteral("fil"), QStringLiteral("Filipino"), QStringLiteral("菲律宾语")},
        {QStringLiteral("hi"), QStringLiteral("Hindi"), QStringLiteral("印地语")},
        {QStringLiteral("zh-Hant"), QStringLiteral("Traditional Chinese"), QStringLiteral("繁体中文")},
        {QStringLiteral("pl"), QStringLiteral("Polish"), QStringLiteral("波兰语")},
        {QStringLiteral("cs"), QStringLiteral("Czech"), QStringLiteral("捷克语")},
        {QStringLiteral("nl"), QStringLiteral("Dutch"), QStringLiteral("荷兰语")},
        {QStringLiteral("km"), QStringLiteral("Khmer"), QStringLiteral("高棉语")},
        {QStringLiteral("my"), QStringLiteral("Burmese"), QStringLiteral("缅甸语")},
        {QStringLiteral("fa"), QStringLiteral("Persian"), QStringLiteral("波斯语")},
        {QStringLiteral("gu"), QStringLiteral("Gujarati"), QStringLiteral("古吉拉特语")},
        {QStringLiteral("ur"), QStringLiteral("Urdu"), QStringLiteral("乌尔都语")},
        {QStringLiteral("te"), QStringLiteral("Telugu"), QStringLiteral("泰卢固语")},
        {QStringLiteral("mr"), QStringLiteral("Marathi"), QStringLiteral("马拉地语")},
        {QStringLiteral("he"), QStringLiteral("Hebrew"), QStringLiteral("希伯来语")},
        {QStringLiteral("bn"), QStringLiteral("Bengali"), QStringLiteral("孟加拉语")},
        {QStringLiteral("ta"), QStringLiteral("Tamil"), QStringLiteral("泰米尔语")},
        {QStringLiteral("uk"), QStringLiteral("Ukrainian"), QStringLiteral("乌克兰语")},
        {QStringLiteral("bo"), QStringLiteral("Tibetan"), QStringLiteral("藏语")},
        {QStringLiteral("kk"), QStringLiteral("Kazakh"), QStringLiteral("哈萨克语")},
        {QStringLiteral("mn"), QStringLiteral("Mongolian"), QStringLiteral("蒙古语")},
        {QStringLiteral("ug"), QStringLiteral("Uyghur"), QStringLiteral("维吾尔语")},
        {QStringLiteral("yue"), QStringLiteral("Cantonese"), QStringLiteral("粤语")}
    };
    return list;
}

inline int indexOf(const QString& code)
{
    const QVector<LangItem>& list = all();
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).code == code)
            return i;
    }
    return -1;
}

inline QString englishName(const QString& code)
{
    if (code == QLatin1String("auto"))
        return QStringLiteral("the detected language");
    const int index = indexOf(code);
    return index >= 0 ? all().at(index).en : code;
}

inline QString displayName(const QString& code, const QString& uiLanguage)
{
    const int index = indexOf(code);
    if (index < 0)
        return code;
    const LangItem& item = all().at(index);
    return uiLanguage == QLatin1String("zh") ? item.zh : item.en;
}

// Returns the code of the dominant Unicode script in \p text among the
// supported languages, or an empty string when the text contains none of
// those scripts. Latin-script languages share one alphabet and cannot be
// told apart here.
inline QString guessFromScript(const QString& text)
{
    struct ScriptRange
    {
        char16_t from;
        char16_t to;
        const char* code;
    };
    static const ScriptRange ranges[] = {
        {0x0400, 0x052F, "ru"},  // Cyrillic + Supplement
        {0x0590, 0x05FF, "he"},  // Hebrew
        {0x0600, 0x06FF, "ar"},  // Arabic
        {0x0750, 0x077F, "ar"},  // Arabic Supplement
        {0x0900, 0x097F, "hi"},  // Devanagari
        {0x0980, 0x09FF, "bn"},  // Bengali
        {0x0A80, 0x0AFF, "gu"},  // Gujarati
        {0x0B80, 0x0BFF, "ta"},  // Tamil
        {0x0C00, 0x0C7F, "te"},  // Telugu
        {0x0E00, 0x0E7F, "th"},  // Thai
        {0x0F00, 0x0FFF, "bo"},  // Tibetan
        {0x1000, 0x109F, "my"},  // Myanmar
        {0x1100, 0x11FF, "ko"},  // Hangul Jamo
        {0x1780, 0x17FF, "km"},  // Khmer
        {0x1800, 0x18AF, "mn"},  // Mongolian
        {0x3040, 0x30FF, "ja"},  // Hiragana + Katakana
        {0x3130, 0x318F, "ko"},  // Hangul Compatibility Jamo
        {0x31F0, 0x31FF, "ja"},  // Katakana Phonetic Extensions
        {0x3400, 0x4DBF, "zh"},  // CJK Extension A
        {0x4E00, 0x9FFF, "zh"},  // CJK Unified Ideographs
        {0xAC00, 0xD7AF, "ko"},  // Hangul Syllables
        {0xF900, 0xFAFF, "zh"},  // CJK Compatibility Ideographs
        {0xFB50, 0xFDFF, "ar"},  // Arabic Presentation Forms-A
        {0xFE70, 0xFEFF, "ar"},  // Arabic Presentation Forms-B
        {0xFF65, 0xFF9F, "ja"}   // Halfwidth Katakana
    };

    QMap<QString, int> counts;
    for (int i = 0; i < text.size(); ++i) {
        const char16_t unit = text.at(i).unicode();
        if (QChar::isHighSurrogate(unit)) {
            const bool paired = ++i < text.size() && QChar::isLowSurrogate(text.at(i).unicode());
            const uint supplementary =
                paired ? QChar::surrogateToUcs4(unit, text.at(i).unicode()) : 0;
            if (supplementary >= 0x20000 && supplementary <= 0x2A6DF)
                ++counts[QStringLiteral("zh")];  // CJK Extension B
            continue;
        }
        for (const ScriptRange& range : ranges) {
            if (unit >= range.from && unit <= range.to) {
                ++counts[QString::fromLatin1(range.code)];
                break;
            }
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
