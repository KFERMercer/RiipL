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
        {QStringLiteral("formal"), QStringLiteral("Formal"), QStringLiteral("正式")},
        {QStringLiteral("casual"), QStringLiteral("Casual"), QStringLiteral("口语")},
        {QStringLiteral("neutral"), QStringLiteral("Neutral"), QStringLiteral("中性")},
        {QStringLiteral("technical"), QStringLiteral("Technical"), QStringLiteral("技术")},
        {QStringLiteral("marketing"), QStringLiteral("Marketing"), QStringLiteral("营销")},
        {QStringLiteral("literary"), QStringLiteral("Literary"), QStringLiteral("文学")},
        {QStringLiteral("academic"), QStringLiteral("Academic"), QStringLiteral("学术")},
        {QStringLiteral("legal"), QStringLiteral("Legal"), QStringLiteral("法律")},
        {QStringLiteral("literal"), QStringLiteral("Literal"), QStringLiteral("直译")},
        {QStringLiteral("idiomatic"), QStringLiteral("Idiomatic"), QStringLiteral("意译")},
        {QStringLiteral("transcreation"), QStringLiteral("Transcreation"), QStringLiteral("创译")},
        {QStringLiteral("machine-like"), QStringLiteral("Machine-like"), QStringLiteral("机器")},
        {QStringLiteral("concise"), QStringLiteral("Concise"), QStringLiteral("简明")}
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
