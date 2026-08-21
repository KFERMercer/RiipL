#pragma once

#include <QString>
#include <QVector>

struct ToneItem
{
    QString key;
    QString en;
    QString zh;
};

namespace Tones {

inline const QVector<ToneItem>& presets()
{
    static const QVector<ToneItem> list = {
        {QStringLiteral("formal"), QStringLiteral("Formal style"), QStringLiteral("正式风格")},
        {QStringLiteral("casual"), QStringLiteral("Casual style"), QStringLiteral("口语风格")},
        {QStringLiteral("neutral"), QStringLiteral("Neutral style"), QStringLiteral("中性风格")},
        {QStringLiteral("technical"), QStringLiteral("Technical style"), QStringLiteral("技术风格")},
        {QStringLiteral("marketing"), QStringLiteral("Marketing style"), QStringLiteral("营销风格")},
        {QStringLiteral("literary"), QStringLiteral("Literary style"), QStringLiteral("文学风格")},
        {QStringLiteral("academic"), QStringLiteral("Academic style"), QStringLiteral("学术风格")},
        {QStringLiteral("legal"), QStringLiteral("Legal style"), QStringLiteral("法律风格")},
        {QStringLiteral("literal"), QStringLiteral("Literal style"), QStringLiteral("直译风格")},
        {QStringLiteral("idiomatic"), QStringLiteral("Idiomatic style"), QStringLiteral("意译风格")},
        {QStringLiteral("transcreation"), QStringLiteral("Transcreation"), QStringLiteral("创译风格")},
        {QStringLiteral("machine-like"), QStringLiteral("Machine-like style"), QStringLiteral("机器风格")},
        {QStringLiteral("concise"), QStringLiteral("Concise style"), QStringLiteral("简明风格")}
    };
    return list;
}

inline QString presetDisplayName(const QString& key, const QString& uiLanguage)
{
    const QVector<ToneItem>& list = presets();
    for (const ToneItem& item : list) {
        if (item.key == key)
            return uiLanguage == QLatin1String("zh") ? item.zh : item.en;
    }
    return key;
}

}
