#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <QtTest>

#include "core/translation/TranslationEngine.h"

namespace TestSupport {

// Directory private to the running test function. The temporary root lives for
// the whole process, so suites that build a store per case never share a path.
inline QString tempDir()
{
    static QTemporaryDir dir;
    return dir.path() + QStringLiteral("/%1").arg(QTest::currentTestFunction());
}

// Content of the user message in a recorded chat-completions request body.
inline QString requestUserPrompt(const QByteArray& body)
{
    const QJsonArray messages =
        QJsonDocument::fromJson(body).object().value(QStringLiteral("messages")).toArray();
    for (const QJsonValue& message : messages) {
        const QJsonObject object = message.toObject();
        if (object.value(QStringLiteral("role")).toString() == QLatin1String("user"))
            return object.value(QStringLiteral("content")).toString();
    }
    return QString();
}

inline QStringList optionTexts(const TranslationEngine::CandidateGroup& group)
{
    QStringList texts;
    for (const TranslationEngine::CandidateOption& option : group.options)
        texts << option.text;
    return texts;
}

}