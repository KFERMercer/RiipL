#pragma once

#include <QJsonArray>
#include <QJsonDocument>
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

inline bool equals(const QJsonValue& a, const QJsonValue& b)
{
    if (a.type() != b.type())
        return false;
    switch (a.type()) {
    case QJsonValue::Bool:
        return a.toBool() == b.toBool();
    case QJsonValue::Double:
        if (a.toDouble() == b.toDouble())
            return true;
        return qFuzzyCompare(a.toDouble(), b.toDouble());
    case QJsonValue::String:
        return a.toString() == b.toString();
    case QJsonValue::Array: {
        const QJsonArray aa = a.toArray();
        const QJsonArray ba = b.toArray();
        if (aa.size() != ba.size())
            return false;
        for (int i = 0; i < aa.size(); ++i) {
            if (!equals(aa.at(i), ba.at(i)))
                return false;
        }
        return true;
    }
    case QJsonValue::Object: {
        const QJsonObject ao = a.toObject();
        const QJsonObject bo = b.toObject();
        if (ao.keys() != bo.keys())
            return false;
        for (const QString& k : ao.keys()) {
            if (!equals(ao.value(k), bo.value(k)))
                return false;
        }
        return true;
    }
    default:
        return true;
    }
}

inline QString compactJson(const QJsonValue& value)
{
    switch (value.type()) {
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    case QJsonValue::Array:
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: {
        const double d = value.toDouble();
        if (double(int(d)) == d && qAbs(d) < 1e15)
            return QString::number(int(d));
        return QString::number(d);
    }
    default:
        return QString();
    }
}

}
