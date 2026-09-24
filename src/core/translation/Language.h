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
