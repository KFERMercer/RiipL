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

// An object whose keys are all line numbers: the shape a document window has.
inline bool isNumberedObject(const QJsonObject& object)
{
    if (object.isEmpty())
        return false;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        bool numbered = false;
        it.key().toInt(&numbered);
        if (!numbered)
            return false;
    }
    return true;
}

// Window a document request asks about: the last JSON object keyed by line
// numbers, the neighbouring segments being plain text.
inline QJsonObject documentWindowIn(const QString& prompt)
{
    QJsonObject window;
    int start = -1;
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (int at = 0; at < prompt.size(); ++at) {
        const QChar character = prompt.at(at);
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (character == QLatin1Char('\\'))
                escaped = true;
            else if (character == QLatin1Char('"'))
                quoted = false;
            continue;
        }
        if (character == QLatin1Char('"')) {
            quoted = true;
        } else if (character == QLatin1Char('{')) {
            if (depth++ == 0)
                start = at;
        } else if (character == QLatin1Char('}') && depth > 0 && --depth == 0) {
            const QJsonObject candidate =
                QJsonDocument::fromJson(prompt.mid(start, at - start + 1).toUtf8()).object();
            if (isNumberedObject(candidate))
                window = candidate;
        }
    }
    return window;
}

}
