#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QVector>

#include <QtTest>

#include <functional>
#include <memory>
#include <utility>

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

// Answers chat-completions requests over a local socket, holding each one back until
// the test releases it.
class StubChatServer
{
public:
    using ReplyContent = std::function<QByteArray(const QString&)>;

    explicit StubChatServer(ReplyContent replyContent)
        : m_replyContent(std::move(replyContent))
    {
        m_server.listen(QHostAddress::LocalHost);
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server,
                         [this]() { accept(m_server.nextPendingConnection()); });
    }

    quint16 port() const { return m_server.serverPort(); }

    bool hold = true;
    int failuresLeft = 0;
    int requests = 0;
    QStringList prompts;

    void releaseAll()
    {
        hold = false;
        const QVector<Held> held = std::exchange(m_held, {});
        for (const Held& item : held)
            answer(item.socket, item.prompt, takeFailure());
    }

    void releaseNext(bool failure)
    {
        if (m_held.isEmpty())
            return;
        const Held item = m_held.takeFirst();
        answer(item.socket, item.prompt, failure);
    }

    // Drops what the last test left behind; each suite sets its own hold.
    void reset()
    {
        failuresLeft = 0;
        requests = 0;
        prompts.clear();
        m_held.clear();
    }

private:
    struct Held
    {
        QTcpSocket* socket = nullptr;
        QString prompt;
    };

    bool takeFailure()
    {
        if (failuresLeft <= 0)
            return false;
        --failuresLeft;
        return true;
    }

    void accept(QTcpSocket* socket)
    {
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const QString request = QString::fromUtf8(*buffer);
            const int headerEnd = request.indexOf(QStringLiteral("\r\n\r\n"));
            if (headerEnd < 0)
                return;
            const int lengthAt = request.indexOf(QStringLiteral("Content-Length:"), 0,
                                                 Qt::CaseInsensitive);
            const int length = request.mid(lengthAt).section(QLatin1Char(':'), 1, 1)
                                   .section(QStringLiteral("\r\n"), 0, 0).trimmed().toInt();
            if (buffer->size() < headerEnd + 4 + length)
                return;

            const QString prompt = requestUserPrompt(buffer->mid(headerEnd + 4, length));
            ++requests;
            prompts.append(prompt);
            if (hold)
                m_held.append({socket, prompt});
            else
                answer(socket, prompt, takeFailure());
        });
    }

    void answer(QTcpSocket* socket, const QString& prompt, bool failure)
    {
        // A cancelled request leaves its connection closed before the test releases it.
        if (socket->state() != QAbstractSocket::ConnectedState) {
            socket->deleteLater();
            return;
        }
        const QByteArray payload = failure
            ? QByteArrayLiteral(R"({"error":{"message":"busy"}})")
            : m_replyContent(prompt);
        const QByteArray status = failure ? QByteArrayLiteral("500 Internal Server Error")
                                          : QByteArrayLiteral("200 OK");
        socket->write("HTTP/1.1 " + status
                      + "\r\nContent-Type: application/json\r\nContent-Length: "
                      + QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
        socket->flush();
        socket->disconnectFromHost();
    }

    ReplyContent m_replyContent;
    QTcpServer m_server;
    QVector<Held> m_held;
};

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
