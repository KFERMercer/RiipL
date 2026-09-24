#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace JsonUtils {

inline QStringList splitPath(const QString& key)
{
    return key.split(QLatin1Char('.'), Qt::SkipEmptyParts);
}

inline QJsonValue getByPath(const QJsonObject& root, const QString& key)
{
    QJsonValue current(root);
    const QStringList parts = splitPath(key);
    for (const QString& part : parts) {
        if (!current.isObject())
            return QJsonValue(QJsonValue::Undefined);
        current = current.toObject().value(part);
    }
    return current;
}

inline QJsonObject setRecursive(QJsonObject obj, const QStringList& parts, int index, const QJsonValue& value)
{
    if (index == parts.size() - 1) {
        obj.insert(parts.at(index), value);
        return obj;
    }
    QJsonObject child = obj.value(parts.at(index)).toObject();
    obj.insert(parts.at(index), setRecursive(child, parts, index + 1, value));
    return obj;
}

inline void setByPath(QJsonObject& root, const QString& key, const QJsonValue& value)
{
    const QStringList parts = splitPath(key);
    if (parts.isEmpty())
        return;
    root = setRecursive(root, parts, 0, value);
}

inline QJsonObject removeRecursive(QJsonObject obj, const QStringList& parts, int index)
{
    if (!obj.contains(parts.at(index)))
        return obj;
    if (index == parts.size() - 1) {
        obj.remove(parts.at(index));
        return obj;
    }
    QJsonObject child = removeRecursive(obj.value(parts.at(index)).toObject(), parts, index + 1);
    if (child.isEmpty())
        obj.remove(parts.at(index));
    else
        obj.insert(parts.at(index), child);
    return obj;
}

inline void removeByPath(QJsonObject& root, const QString& key)
{
    const QStringList parts = splitPath(key);
    if (parts.isEmpty())
        return;
    root = removeRecursive(root, parts, 0);
}

}
