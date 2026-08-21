#pragma once

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

}
