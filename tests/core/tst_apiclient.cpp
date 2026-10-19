#include <QtTest>

#include "TestSupport.h"
#include "core/config/Defaults.h"
#include "core/config/ConfigManager.h"
#include "core/network/ApiClient.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QHttpHeaders>
#include <QTcpServer>
#include <QTcpSocket>

#include <optional>

namespace {

// Serves the queued replies to the requests that reach it, one per connection.
class OneShotServer
{
public:
    OneShotServer()
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() {
            QTcpSocket* socket = m_server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                m_requests.append(socket->readAll());
                const Reply reply = m_replies.isEmpty() ? Reply{} : m_replies.takeFirst();
                socket->write(reply.body);
                socket->flush();
                if (!reply.heldOpen)
                    socket->disconnectFromHost();
            });
        });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost); }
    quint16 port() const { return m_server.serverPort(); }

    // A held open connection is how an endpoint that never closes a stream behaves.
    void serve(const QByteArray& response, bool heldOpen = false)
    {
        m_replies.append({response, heldOpen});
    }

    // Raw bytes of every request that reached the server.
    const QList<QByteArray>& requests() const { return m_requests; }

private:
    struct Reply
    {
        QByteArray body;
        bool heldOpen = false;
    };

    QTcpServer m_server;
    QList<Reply> m_replies;
    QList<QByteArray> m_requests;
};

QByteArray jsonReply(const QByteArray& body)
{
    return QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n") + body;
}

// A JSON body sent with a failure status.
QByteArray failureReply(const char* status, const QByteArray& body)
{
    return QByteArrayLiteral("HTTP/1.1 ") + status
        + QByteArrayLiteral("\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n") + body;
}

// A JSON body sent with a failure status under a non-JSON content type.
QByteArray textReply(const char* status, const QByteArray& body)
{
    return QByteArrayLiteral("HTTP/1.1 ") + status
        + QByteArrayLiteral("\r\nContent-Type: text/plain\r\nContent-Length: ")
        + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n") + body;
}

// Closes the connection with the reply, so the next request opens its own.
QByteArray closingJsonReply(const QByteArray& body)
{
    return QByteArrayLiteral("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n") + body;
}

// A stream carries no length: its frames run until the server closes.
QByteArray streamReply(const QByteArray& frames)
{
    return QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\n") + frames;
}

QByteArray streamFrame(const char* content)
{
    return QByteArrayLiteral("data: {\"choices\":[{\"delta\":{\"content\":\"") + content
        + QByteArrayLiteral("\"}}]}");
}

// A stream whose transfer breaks: one whole chunk, then the connection closes
// before the terminating chunk.
QByteArray brokenStreamReply(const QByteArray& frames)
{
    return QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                             "Transfer-Encoding: chunked\r\n\r\n")
        + QByteArray::number(frames.size(), 16) + QByteArrayLiteral("\r\n") + frames;
}

// Frame the endpoint sends when it fails after the status line has gone out.
QByteArray streamErrorFrame(const char* type, const char* message)
{
    return QByteArrayLiteral("data: {\"error\":{\"type\":\"") + type
        + QByteArrayLiteral("\",\"message\":\"") + message
        + QByteArrayLiteral("\",\"param\":null,\"code\":null}}");
}

QJsonObject chatBody()
{
    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("user"));
    message.insert(QStringLiteral("content"), QStringLiteral("hello"));
    QJsonObject body;
    body.insert(QStringLiteral("model"), QStringLiteral("gpt-4o-mini"));
    body.insert(QStringLiteral("messages"), QJsonArray{message});
    body.insert(QStringLiteral("stream"), false);
    return body;
}

// Ids of a parsed model list, in the order the endpoint reported them.
QStringList modelIds(const QList<ApiClient::Model>& models)
{
    QStringList ids;
    ids.reserve(models.size());
    for (const ApiClient::Model& model : models)
        ids.append(model.id);
    return ids;
}

}

class TestApiClient : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void normalizesBaseUrl();
    void requestsDerivedEndpoint();
    void keepsConfiguredUserAgent();
    void parsesCustomHeaderLines();
    void refusesUnusableBaseUrl();
    void refusesUnusableChatBody();
    void reportsIdleBeforeDelivering();
    void keepsResultWhenHandlerStartsNextRequest();
    void reportsIdleTimeout();
    void readsTrailingStreamFrame();
    void readsEventStreamFrames();
    void ignoresChunksWithoutText();
    void reportsStreamRefusal();
    void reportsTruncatedStream();
    void endsStreamAtTerminator();
    void rejectsStreamWithoutContent();
    void reportsStreamErrorFrame();
    void deliversStreamedDeltasOnceBeforeStreamError();
    void reportsStreamErrorTypeWithoutMessage();
    void reportsStreamErrorCodeWithoutMessage();
    void reportsBrokenStreamAsNetworkFailure();
    void reportsBrokenStreamBeforeFlushingTail();
    void keepsStreamWhenDeltaHandlerStartsNextRequest();
    void dropsOversizedResponse();
    void rejectsCompletionWithoutChoices();
    void rejectsCompletionWithoutContent();
    void reportsRefusal();
    void reportsIncompleteCompletion();
    void parsesModels();
    void listsModels();
    void listsModelsFromGivenValues();
    void keepsModelListWhenHandlerStartsNextRequest();
    void reportsModelListFailure();
    void reportsPlainTextFailureBody();
    void dropsOversizedModelList();
    void refusesUnusableModelBaseUrl();
    void reportsArrayWrappedFailure();
};

void TestApiClient::initTestCase()
{
    // The application shell names the client; the suite pins its own values.
    QCoreApplication::setApplicationName(QStringLiteral("TestClient"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.2.3"));
}

void TestApiClient::normalizesBaseUrl()
{
    QCOMPARE(ApiClient::normalizedBaseUrl(QStringLiteral("https://api.example.com/v1///")).toString(),
             QStringLiteral("https://api.example.com/v1"));
    QCOMPARE(ApiClient::normalizedBaseUrl(QStringLiteral("http://localhost:11434")).toString(),
             QStringLiteral("http://localhost:11434"));
    QCOMPARE(ApiClient::normalizedBaseUrl(QStringLiteral("https://example.com/a/b/")).path(),
             QStringLiteral("/a/b"));
}

// Exercises the path the client actually sends, including the trailing-slash
// collapse that normalizedBaseUrl performs before the endpoint is derived.

void TestApiClient::requestsDerivedEndpoint()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1///").arg(server.serverPort()));

    QString requestLine;
    QByteArray authorization;
    QByteArray accept;
    QByteArray contentType;
    QByteArray userAgent;
    QObject::connect(&server, &QTcpServer::newConnection, this, [&]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
            const QByteArray data = socket->readAll();
            if (!requestLine.isEmpty())
                return;
            const QList<QByteArray> lines = data.split('\n');
            requestLine = lines.first().trimmed();
            for (const QByteArray& line : lines) {
                if (line.toLower().startsWith("authorization:"))
                    authorization = line.mid(int(line.indexOf(':')) + 1).trimmed();
                else if (line.toLower().startsWith("accept:"))
                    accept = line.mid(int(line.indexOf(':')) + 1).trimmed();
                else if (line.toLower().startsWith("content-type:"))
                    contentType = line.mid(int(line.indexOf(':')) + 1).trimmed();
                else if (line.toLower().startsWith("user-agent:"))
                    userAgent = line.mid(int(line.indexOf(':')) + 1).trimmed();
            }
            const QByteArray body = R"({"choices":[{"message":{"content":"ok"}}]})";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->flush();
        });
    });

    ConfigManager::instance()->setValue(Keys::apiKey, QStringLiteral("secret"));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    QString result;
    std::optional<ApiClient::Error> error;
    client.sendChatRequest(chatBody(),
                           [&](const QString& text) { result = text; },
                           nullptr,
                           [&](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY2(!error.has_value(), "the request must not report a failure");
    QCOMPARE(result, QStringLiteral("ok"));
    QCOMPARE(requestLine, QStringLiteral("POST /v1/chat/completions HTTP/1.1"));
    QCOMPARE(authorization, QByteArrayLiteral("Bearer secret"));
    QCOMPARE(accept, QByteArrayLiteral("application/json"));
    QCOMPARE(contentType, QByteArrayLiteral("application/json"));
    QCOMPARE(userAgent, QByteArrayLiteral("TestClient/1.2.3"));
}

// A User-Agent the user configured replaces the default one.
void TestApiClient::keepsConfiguredUserAgent()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(jsonReply(QByteArrayLiteral(R"({"choices":[{"message":{"content":"ok"}}]})")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));
    ConfigManager::instance()->setValue(Keys::apiCustomHeaders,
                                        QStringLiteral("User-Agent: custom-agent/1"));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    client.sendChatRequest(chatBody(), nullptr, nullptr, nullptr);

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    const QByteArray request = server.requests().first().toLower();
    QVERIFY(request.contains("user-agent: custom-agent/1"));
    QVERIFY(!request.contains("user-agent: testclient/"));
}

void TestApiClient::parsesCustomHeaderLines()
{
    QVERIFY(ApiClient::parseCustomHeaders(QString()).isEmpty());
    QVERIFY(ApiClient::parseCustomHeaders(QStringLiteral("\n   \n")).isEmpty());
    QVERIFY(ApiClient::parseCustomHeaders(QStringLiteral("InvalidHeader\n:Nameless")).isEmpty());
    // Duplicate names are preserved in order; the request layer resolves them.
    QCOMPARE(ApiClient::parseCustomHeaders(QStringLiteral("X-A: 1\nX-A: 2")).size(), 2);

    const QHttpHeaders headers = ApiClient::parseCustomHeaders(
        QStringLiteral("X-Title: RiipL\nAuthorization: Bearer secret\n  X-Retry : 3 \nBroken line"));

    QCOMPARE(headers.size(), 3);
    QCOMPARE(headers.nameAt(0), QByteArrayView("x-title"));
    QCOMPARE(headers.valueAt(0), QByteArrayView("RiipL"));
    QCOMPARE(headers.nameAt(1), QByteArrayView("authorization"));
    QCOMPARE(headers.valueAt(1), QByteArrayView("Bearer secret"));
    QCOMPARE(headers.nameAt(2), QByteArrayView("x-retry"));
    QCOMPARE(headers.valueAt(2), QByteArrayView("3"));
}

// An endpoint that cannot carry the request is named before one is built.
void TestApiClient::refusesUnusableBaseUrl()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    ApiClient client;
    for (const QString& baseUrl : {QStringLiteral("ftp://example.com/v1"),
                                   QStringLiteral("/v1"),
                                   QStringLiteral("not a url")}) {
        ConfigManager::instance()->setValue(Keys::apiBaseUrl, baseUrl);
        QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
        std::optional<ApiClient::Error> error;
        client.sendChatRequest(chatBody(), nullptr, nullptr,
                               [&error](const ApiClient::Error& failure) { error = failure; });
        QCOMPARE(finishedSpy.count(), 1);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, ApiClient::ErrorCode::BaseUrlInvalid);
    }
}

// A body the specification cannot accept is refused before a request exists.
void TestApiClient::refusesUnusableChatBody()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    ConfigManager::instance()->setValue(Keys::apiBaseUrl,
                                        QStringLiteral("http://127.0.0.1:1/v1"));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    const auto send = [&](const QJsonObject& body) {
        finishedSpy.clear();
        std::optional<ApiClient::Error> error;
        client.sendChatRequest(body, nullptr, nullptr,
                               [&error](const ApiClient::Error& failure) { error = failure; });
        return error;
    };

    const QJsonArray message{QJsonObject{}};
    for (const QJsonObject& body : {
             QJsonObject(),
             QJsonObject{{QStringLiteral("model"), QStringLiteral("gpt-4o-mini")}},
             QJsonObject{{QStringLiteral("messages"), message}},
             QJsonObject{{QStringLiteral("model"), QJsonValue()},
                         {QStringLiteral("messages"), message}},
             QJsonObject{{QStringLiteral("model"), QStringLiteral("gpt-4o-mini")},
                         {QStringLiteral("messages"), QStringLiteral("none")}},
             QJsonObject{{QStringLiteral("model"), QStringLiteral("gpt-4o-mini")},
                         {QStringLiteral("messages"), QJsonArray{}}},
         }) {
        const std::optional<ApiClient::Error> error = send(body);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, ApiClient::ErrorCode::InvalidRequest);
    }
    QCOMPARE(finishedSpy.count(), 1);
}

// The client reports itself idle before it hands the result over.
void TestApiClient::reportsIdleBeforeDelivering()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(jsonReply(QByteArrayLiteral(R"({"choices":[{"message":{"content":"ok"}}]})")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QStringList order;
    QObject::connect(&client, &ApiClient::requestFinished, &client,
                     [&order]() { order << QStringLiteral("finished"); });
    client.sendChatRequest(chatBody(),
                           [&order](const QString&) { order << QStringLiteral("done"); },
                           nullptr, nullptr);

    QVERIFY(TestSupport::waitFor([&] { return order.size() == 2; }, 5000));
    QCOMPARE(order.size(), 2);
    QCOMPARE(order, QStringList({QStringLiteral("finished"), QStringLiteral("done")}));
}

// A slot that starts the next request must not change what the finished one delivers.
void TestApiClient::keepsResultWhenHandlerStartsNextRequest()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(closingJsonReply(QByteArrayLiteral(R"({"choices":[{"message":{"content":"first"}}]})")));
    server.serve(closingJsonReply(QByteArrayLiteral(R"({"choices":[{"message":{"content":"second"}}]})")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QStringList results;
    std::optional<ApiClient::Error> error;
    bool nextRequestSent = false;
    QObject::connect(&client, &ApiClient::requestFinished, &client, [&]() {
        if (nextRequestSent)
            return;
        nextRequestSent = true;
        client.sendChatRequest(chatBody(),
                               [&results](const QString& text) { results << text; },
                               nullptr, nullptr);
    });
    client.sendChatRequest(chatBody(),
                           [&results](const QString& text) { results << text; },
                           nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return results.size() == 2; }, 5000));
    QCOMPARE(results.size(), 2);
    QVERIFY2(!error.has_value(), "the next request must not change this one's outcome");
    QCOMPARE(results, QStringList({QStringLiteral("first"), QStringLiteral("second")}));
}

// A server that never answers leaves the transfer timeout as the only end.
void TestApiClient::reportsIdleTimeout()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()));
    ConfigManager::instance()->setValue(Keys::apiTimeoutMs, ApiTimeout::minimumMs);

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    std::optional<ApiClient::Error> error;
    client.sendChatRequest(chatBody(), nullptr, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 10000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ApiClient::ErrorCode::TimedOut);
}

// The last frame arrives without a closing newline when the connection ends.
void TestApiClient::readsTrailingStreamFrame()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(streamReply(streamFrame("Hel") + "\n\n" + streamFrame("lo")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    QString result;
    std::optional<ApiClient::Error> error;
    QJsonObject body = chatBody();
    body.insert(QStringLiteral("stream"), true);
    client.sendChatRequest(body, [&result](const QString& text) { result = text; }, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY2(!error.has_value(), "the trailing frame must be read");
    QCOMPARE(result, QStringLiteral("Hello"));
}

// The terminator ends the transfer instead of the endpoint closing it.
void TestApiClient::endsStreamAtTerminator()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(streamReply(streamFrame("Hel") + "\n\n" + streamFrame("lo")
                             + "\n\ndata: [DONE]\n\n"),
                 true);
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    QString result;
    std::optional<ApiClient::Error> error;
    QJsonObject body = chatBody();
    body.insert(QStringLiteral("stream"), true);
    client.sendChatRequest(body, [&result](const QString& text) { result = text; }, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    // The default timeout leaves the endpoint enough time to hold the connection
    // open, so only the terminator can end the transfer.
    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 2000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY2(!error.has_value(), "the terminator settles the request");
    QCOMPARE(result, QStringLiteral("Hello"));
    QVERIFY(server.requests().first().toLower().contains("accept: text/event-stream"));
}

void TestApiClient::readsEventStreamFrames()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    // A blank line without data and comments are skipped, the event type names
    // nothing, multi-line data is joined, and CRLF is accepted.
    server.serve(streamReply(QByteArrayLiteral(
        "\r\n"
        ": keep-alive\r\n"
        "event: message\r\n"
        "data: {\"choices\":[{\"delta\":\r\n"
        "data: {\"content\":\"Hel\"}}]}\r\n"
        "\r\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"lo\"}}]}\r\n"
        "\r\n"
        "data: [DONE]\r\n"
        "\r\n")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    QString result;
    std::optional<ApiClient::Error> error;
    QJsonObject body = chatBody();
    body.insert(QStringLiteral("stream"), true);
    client.sendChatRequest(body, [&result](const QString& text) { result = text; }, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY2(!error.has_value(), "every frame must parse");
    QCOMPARE(result, QStringLiteral("Hello"));
}

// A stream with usage reports a chunk without choices, and a chunk may carry
// null content; neither adds text.
void TestApiClient::ignoresChunksWithoutText()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(streamReply(
        QByteArrayLiteral("data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hel\"}}]}\n\n"
                          "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":null}}]}\n\n"
                          "data: {\"choices\":[],\"usage\":{\"total_tokens\":3}}\n\n"
                          "data: [DONE]\n\n")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    QString result;
    std::optional<ApiClient::Error> error;
    QJsonObject body = chatBody();
    body.insert(QStringLiteral("stream"), true);
    client.sendChatRequest(body, [&result](const QString& text) { result = text; }, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY2(!error.has_value(), "the chunks without text must be ignored");
    QCOMPARE(result, QStringLiteral("Hel"));
}

// A refusal delta explains why a stream produced no text.
void TestApiClient::reportsStreamRefusal()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(streamReply(QByteArrayLiteral(
        "data: {\"choices\":[{\"index\":0,\"delta\":{\"refusal\":\"I cannot help with that.\"},\"finish_reason\":null}]}\n\n"
        "data: [DONE]\n\n")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    std::optional<ApiClient::Error> error;
    QJsonObject body = chatBody();
    body.insert(QStringLiteral("stream"), true);
    client.sendChatRequest(body, nullptr, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ApiClient::ErrorCode::ServerMessage);
    QCOMPARE(error->detail, QStringLiteral("I cannot help with that."));
}

// A stream whose last chunk names the token limit stopped before the answer ended.
void TestApiClient::reportsTruncatedStream()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(streamReply(QByteArrayLiteral(
        "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hel\"},\"finish_reason\":null}]}\n\n"
        "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"length\"}]}\n\n"
        "data: [DONE]\n\n")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    std::optional<ApiClient::Error> error;
    QJsonObject body = chatBody();
    body.insert(QStringLiteral("stream"), true);
    client.sendChatRequest(body, nullptr, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ApiClient::ErrorCode::Truncated);
}

// A stream without text is a failure, not an empty translation.
void TestApiClient::rejectsStreamWithoutContent()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(streamReply(QByteArrayLiteral("data: [DONE]\n\n")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    std::optional<ApiClient::Error> error;
    QJsonObject body = chatBody();
    body.insert(QStringLiteral("stream"), true);
    client.sendChatRequest(body, nullptr, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ApiClient::ErrorCode::NoContent);
}

// A failure the endpoint reaches mid-answer arrives as a frame of the stream.
void TestApiClient::reportsStreamErrorFrame()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(streamReply(streamFrame("Hel") + "\n\n"
                             + streamErrorFrame("invalid_request_error", "Context size has been exceeded.")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    QString result;
    std::optional<ApiClient::Error> error;
    QJsonObject body = chatBody();
    body.insert(QStringLiteral("stream"), true);
    client.sendChatRequest(body, [&result](const QString& text) { result = text; }, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ApiClient::ErrorCode::ServerMessage);
    QCOMPARE(error->detail, QStringLiteral("Context size has been exceeded."));
    QVERIFY2(result.isEmpty(), "the part sent before the failure is not an answer");
}

// The answer the endpoint sent before it failed is delivered once, however many
// frames shared the read the failure arrived in.
void TestApiClient::deliversStreamedDeltasOnceBeforeStreamError()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(streamReply(streamFrame("Hel") + "\n\n"
                             + streamErrorFrame("invalid_request_error", "Context size has been exceeded.")
                             + "\n\n"));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    QStringList deltas;
    std::optional<ApiClient::Error> error;
    QJsonObject body = chatBody();
    body.insert(QStringLiteral("stream"), true);
    client.sendChatRequest(body, nullptr,
                           [&deltas](const QString& piece) { deltas << piece; },
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(error.has_value());
    QCOMPARE(deltas.join(QString()), QStringLiteral("Hel"));
}

// An error object without its required message still names the failure by type.
void TestApiClient::reportsStreamErrorTypeWithoutMessage()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(streamReply(streamFrame("Hel") + "\n\n"
                             + QByteArrayLiteral("data: {\"error\":{\"type\":\"server_error\",\"param\":null,\"code\":null}}")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    std::optional<ApiClient::Error> error;
    QJsonObject body = chatBody();
    body.insert(QStringLiteral("stream"), true);
    client.sendChatRequest(body, nullptr, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ApiClient::ErrorCode::ServerMessage);
    QCOMPARE(error->detail, QStringLiteral("server_error"));
}

// The code an error object carries names the failure ahead of its type.
void TestApiClient::reportsStreamErrorCodeWithoutMessage()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(streamReply(
        streamFrame("Hel") + "\n\n"
        + QByteArrayLiteral("data: {\"error\":{\"type\":\"invalid_request_error\",\"param\":null,\"code\":\"context_length_exceeded\"}}")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    std::optional<ApiClient::Error> error;
    QJsonObject body = chatBody();
    body.insert(QStringLiteral("stream"), true);
    client.sendChatRequest(body, nullptr, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ApiClient::ErrorCode::ServerMessage);
    QCOMPARE(error->detail, QStringLiteral("context_length_exceeded"));
}

// A transfer the endpoint breaks under a success status is the failure, not the
// status it already sent.
void TestApiClient::reportsBrokenStreamAsNetworkFailure()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(brokenStreamReply(streamFrame("Hel") + "\n\n"));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    QString result;
    std::optional<ApiClient::Error> error;
    QJsonObject body = chatBody();
    body.insert(QStringLiteral("stream"), true);
    client.sendChatRequest(body, [&result](const QString& text) { result = text; }, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ApiClient::ErrorCode::NetworkFailure);
    QVERIFY2(result.isEmpty(), "a stream cut off before its terminator is not an answer");
}

// A transfer that breaks mid-frame is the transport failure, not a parse error
// of the bytes the interrupted frame left behind.
void TestApiClient::reportsBrokenStreamBeforeFlushingTail()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(brokenStreamReply(
        streamFrame("Hel") + "\n\n"
        + QByteArrayLiteral("data: {\"choices\":[{\"delta\":{\"content\":\"lo")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    std::optional<ApiClient::Error> error;
    QJsonObject body = chatBody();
    body.insert(QStringLiteral("stream"), true);
    client.sendChatRequest(body, nullptr, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ApiClient::ErrorCode::NetworkFailure);
}

// A delta slot may start the next request while a stream is being parsed.
void TestApiClient::keepsStreamWhenDeltaHandlerStartsNextRequest()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(streamReply(streamFrame("Hel") + "\n\n" + streamFrame("lo")
                             + "\n\ndata: [DONE]\n\n"),
                 true);
    server.serve(closingJsonReply(QByteArrayLiteral(R"({"choices":[{"message":{"content":"second"}}]})")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QString secondResult;
    std::optional<ApiClient::Error> error;
    bool firstDone = false;
    bool nextRequestSent = false;
    QJsonObject body = chatBody();
    body.insert(QStringLiteral("stream"), true);
    client.sendChatRequest(
        body, [&firstDone](const QString&) { firstDone = true; },
        [&](const QString&) {
            if (nextRequestSent)
                return;
            nextRequestSent = true;
            client.sendChatRequest(chatBody(),
                                   [&secondResult](const QString& text) { secondResult = text; },
                                   nullptr,
                                   [&error](const ApiClient::Error& failure) { error = failure; });
        },
        nullptr);

    QVERIFY(TestSupport::waitFor([&] { return !secondResult.isEmpty(); }, 5000));
    QVERIFY2(!error.has_value(), "the next request must settle normally");
    QCOMPARE(secondResult, QStringLiteral("second"));
    QVERIFY2(!firstDone, "the replaced stream reports no answer");
}

// A body past the cap is dropped instead of being buffered to the end.
void TestApiClient::dropsOversizedResponse()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    const QByteArray filler(5 * 1024 * 1024, 'a');
    server.serve(jsonReply(QByteArrayLiteral(R"({"choices":[{"message":{"content":")") + filler
                                              + QByteArrayLiteral(R"("}}]})")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    std::optional<ApiClient::Error> error;
    client.sendChatRequest(chatBody(), nullptr, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 15000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ApiClient::ErrorCode::ResponseTooLarge);
}

// A body without the required choices array is a parse failure; an empty one
// names no choice.
void TestApiClient::rejectsCompletionWithoutChoices()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(closingJsonReply(QByteArrayLiteral(R"({"object":"chat.completion"})")));
    server.serve(closingJsonReply(QByteArrayLiteral(R"({"choices":[]})")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    QList<ApiClient::Error> failures;
    const auto send = [&]() {
        client.sendChatRequest(chatBody(), nullptr, nullptr,
                               [&failures](const ApiClient::Error& failure) { failures << failure; });
    };

    send();
    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    send();
    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 2; }, 5000));

    QCOMPARE(failures.size(), 2);
    QCOMPARE(failures.at(0).code, ApiClient::ErrorCode::InvalidResponse);
    QCOMPARE(failures.at(1).code, ApiClient::ErrorCode::NoChoices);
}

// A message whose content is null is an unusable completion when it carries no
// refusal to explain it.
void TestApiClient::rejectsCompletionWithoutContent()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(jsonReply(QByteArrayLiteral(
        R"({"choices":[{"index":0,"message":{"role":"assistant","content":null},"finish_reason":"stop","logprobs":null}]})")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    std::optional<ApiClient::Error> error;
    client.sendChatRequest(chatBody(), nullptr, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ApiClient::ErrorCode::NoContent);
}

// A refusal is the model's own wording for why it produced no text.
void TestApiClient::reportsRefusal()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(jsonReply(QByteArrayLiteral(
        R"({"choices":[{"index":0,"message":{"role":"assistant","content":null,"refusal":"I cannot help with that."},"finish_reason":"stop","logprobs":null}]})")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    std::optional<ApiClient::Error> error;
    client.sendChatRequest(chatBody(), nullptr, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ApiClient::ErrorCode::ServerMessage);
    QCOMPARE(error->detail, QStringLiteral("I cannot help with that."));
}

// A finish reason that names an incomplete answer is a failure even though the
// response carries text.
void TestApiClient::reportsIncompleteCompletion()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(closingJsonReply(QByteArrayLiteral(
        R"({"choices":[{"index":0,"message":{"role":"assistant","content":"partial"},"finish_reason":"length","logprobs":null}]})")));
    server.serve(closingJsonReply(QByteArrayLiteral(
        R"({"choices":[{"index":0,"message":{"role":"assistant","content":null},"finish_reason":"content_filter","logprobs":null}]})")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    QList<ApiClient::Error> failures;
    QStringList results;
    const auto send = [&]() {
        client.sendChatRequest(chatBody(),
                               [&results](const QString& text) { results << text; }, nullptr,
                               [&failures](const ApiClient::Error& failure) { failures << failure; });
    };

    send();
    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    send();
    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 2; }, 5000));

    QCOMPARE(failures.size(), 2);
    QCOMPARE(failures.at(0).code, ApiClient::ErrorCode::Truncated);
    QCOMPARE(failures.at(1).code, ApiClient::ErrorCode::Filtered);
    QVERIFY2(results.isEmpty(), "an incomplete answer is not a result");
}

// A body without a model array names nothing, which is not the same as an
// endpoint that reports no model.
void TestApiClient::parsesModels()
{
    QVERIFY(!ApiClient::parseModels(QByteArrayLiteral("not json")).has_value());
    QVERIFY(!ApiClient::parseModels(QByteArrayLiteral(R"({"object":"list"})")).has_value());
    QVERIFY(!ApiClient::parseModels(QByteArrayLiteral(R"({"data":"none"})")).has_value());

    // Entries without an id are skipped, the order the endpoint reports is kept,
    // and the label falls back from name to display_name and displayName.
    const std::optional<QList<ApiClient::Model>> models = ApiClient::parseModels(
        QByteArrayLiteral(R"({"data":[{"id":"gpt-4o-mini","object":"model","created":1686935002,"owned_by":"openai","name":"GPT-4o mini","display_name":"Ignored","displayName":"Ignored"},{"id":"models/gemini-2.5-flash","object":"model","owned_by":"google","display_name":"Gemini 2.5 Flash","displayName":"Ignored"},{"id":"vertex-model","object":"model","owned_by":"google","displayName":"Vertex Model"},{"id":""},{},"o3-mini",{"id":"gpt-4.1-mini"}]})"));
    QVERIFY(models.has_value());
    QCOMPARE(modelIds(*models), QStringList({QStringLiteral("gpt-4o-mini"),
                                             QStringLiteral("models/gemini-2.5-flash"),
                                             QStringLiteral("vertex-model"),
                                             QStringLiteral("gpt-4.1-mini")}));
    QCOMPARE(models->at(0).name, QStringLiteral("GPT-4o mini"));
    QCOMPARE(models->at(1).name, QStringLiteral("Gemini 2.5 Flash"));
    QCOMPARE(models->at(2).name, QStringLiteral("Vertex Model"));
    QVERIFY(models->at(3).name.isEmpty());

    const std::optional<QList<ApiClient::Model>> empty = ApiClient::parseModels(QByteArrayLiteral(R"({"data":[]})"));
    QVERIFY(empty.has_value());
    QVERIFY(empty->isEmpty());
}

// The listing is a plain GET below the configured base URL.
void TestApiClient::listsModels()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(jsonReply(
        QByteArrayLiteral(R"({"object":"list","data":[{"id":"gpt-4o-mini"},{"id":"o3-mini"}]})")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));
    ConfigManager::instance()->setValue(Keys::apiKey, QStringLiteral("secret"));

    ApiClient client;
    // The listing settles the client like any other request.
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    QList<ApiClient::Model> models;
    std::optional<ApiClient::Error> error;
    client.listModels(ApiClient::Connection::configured(),
                      [&models](const QList<ApiClient::Model>& listed) { models = listed; },
                      [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return models.size() == 2; }, 5000));
    QCOMPARE(models.size(), 2);
    QVERIFY2(!error.has_value(), "the listing must not report a failure");
    QCOMPARE(modelIds(models), QStringList({QStringLiteral("gpt-4o-mini"), QStringLiteral("o3-mini")}));
    QCOMPARE(finishedSpy.count(), 1);

    QCOMPARE(server.requests().size(), 1);
    const QByteArray request = server.requests().first();
    QCOMPARE(request.split('\n').first().trimmed(), QByteArrayLiteral("GET /v1/models HTTP/1.1"));
    const QByteArray lowered = request.toLower();
    QVERIFY(lowered.contains("authorization: bearer secret"));
    QVERIFY(lowered.contains("accept: application/json"));
}

// Values a caller holds take precedence over the stored configuration.
void TestApiClient::listsModelsFromGivenValues()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    // Stored settings that cannot carry a request: only the given ones may.
    ConfigManager::instance()->setValue(Keys::apiBaseUrl, QString());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(jsonReply(QByteArrayLiteral(R"({"data":[{"id":"local-model"}]})")));

    QJsonObject values;
    values.insert(Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));
    values.insert(Keys::apiKey, QStringLiteral("given-key"));
    values.insert(Keys::apiCustomHeaders, QStringLiteral("X-Probe: 1"));
    values.insert(Keys::apiTimeoutMs, 5000);

    ApiClient client;
    QList<ApiClient::Model> models;
    std::optional<ApiClient::Error> error;
    client.listModels(ApiClient::Connection::fromValues(values),
                      [&models](const QList<ApiClient::Model>& listed) { models = listed; },
                      [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return models.size() == 1; }, 5000));
    QCOMPARE(models.size(), 1);
    QVERIFY2(!error.has_value(), "the given settings must carry the request");
    QCOMPARE(modelIds(models), QStringList({QStringLiteral("local-model")}));

    QCOMPARE(server.requests().size(), 1);
    const QByteArray request = server.requests().first();
    QCOMPARE(request.split('\n').first().trimmed(), QByteArrayLiteral("GET /v1/models HTTP/1.1"));
    const QByteArray lowered = request.toLower();
    QVERIFY(lowered.contains("authorization: bearer given-key"));
    QVERIFY(lowered.contains("x-probe: 1"));
    QVERIFY(lowered.contains("user-agent: testclient/"));
}

// A slot that starts the next listing must not change what the finished one
// delivers.
void TestApiClient::keepsModelListWhenHandlerStartsNextRequest()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(closingJsonReply(QByteArrayLiteral(R"({"data":[{"id":"first"}]})")));
    server.serve(closingJsonReply(QByteArrayLiteral(R"({"data":[{"id":"second"}]})")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QList<QStringList> listings;
    std::optional<ApiClient::Error> error;
    bool nextRequestSent = false;
    QObject::connect(&client, &ApiClient::requestFinished, &client, [&]() {
        if (nextRequestSent)
            return;
        nextRequestSent = true;
        client.listModels(ApiClient::Connection::configured(),
                          [&listings](const QList<ApiClient::Model>& models) {
                              listings << modelIds(models);
                          },
                          [&error](const ApiClient::Error& failure) { error = failure; });
    });
    client.listModels(ApiClient::Connection::configured(),
                      [&listings](const QList<ApiClient::Model>& models) {
                          listings << modelIds(models);
                      },
                      [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return listings.size() == 2; }, 5000));
    QVERIFY2(!error.has_value(), "the next listing must not change this one's outcome");
    QCOMPARE(listings.at(0), QStringList({QStringLiteral("first")}));
    QCOMPARE(listings.at(1), QStringList({QStringLiteral("second")}));
}

// A request the endpoint describes with its own message reaches the caller.
void TestApiClient::reportsModelListFailure()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(failureReply("401 Unauthorized",
                              QByteArrayLiteral(R"({"error":{"message":"invalid key"}})")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QList<ApiClient::Model> models;
    std::optional<ApiClient::Error> error;
    client.listModels(ApiClient::Connection::configured(),
                      [&models](const QList<ApiClient::Model>& listed) { models = listed; },
                      [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return error.has_value(); }, 5000));
    QCOMPARE(error->code, ApiClient::ErrorCode::ServerMessage);
    QCOMPARE(error->detail, QStringLiteral("invalid key"));
    QVERIFY(models.isEmpty());
}

// An error body is JSON text even when the response labels it text/plain, and the
// models endpoint may explain a permission failure with a bare string.
void TestApiClient::reportsPlainTextFailureBody()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(textReply("401 Unauthorized",
                           QByteArrayLiteral(R"({"error":{"message":"invalid key"}})")));
    server.serve(failureReply("403 Forbidden", QByteArrayLiteral(R"({"error":"missing scope"})")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QList<ApiClient::Error> failures;
    const auto list = [&]() {
        client.listModels(ApiClient::Connection::configured(), nullptr,
                          [&failures](const ApiClient::Error& failure) { failures << failure; });
    };

    list();
    QVERIFY(TestSupport::waitFor([&] { return failures.size() == 1; }, 5000));
    list();
    QVERIFY(TestSupport::waitFor([&] { return failures.size() == 2; }, 5000));

    QCOMPARE(failures.at(0).code, ApiClient::ErrorCode::ServerMessage);
    QCOMPARE(failures.at(0).detail, QStringLiteral("invalid key"));
    QCOMPARE(failures.at(1).code, ApiClient::ErrorCode::ServerMessage);
    QCOMPARE(failures.at(1).detail, QStringLiteral("missing scope"));
}

// A body past the cap is dropped instead of being buffered to the end.
void TestApiClient::dropsOversizedModelList()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    const QByteArray filler(5 * 1024 * 1024, 'a');
    server.serve(jsonReply(QByteArrayLiteral(R"({"data":[{"id":")") + filler
                                             + QByteArrayLiteral(R"("}]})")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QList<ApiClient::Model> models;
    std::optional<ApiClient::Error> error;
    client.listModels(ApiClient::Connection::configured(),
                      [&models](const QList<ApiClient::Model>& listed) { models = listed; },
                      [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return error.has_value(); }, 15000));
    QCOMPARE(error->code, ApiClient::ErrorCode::ResponseTooLarge);
    QVERIFY(models.isEmpty());
}

// An endpoint that cannot carry the request is named before the listing starts.
void TestApiClient::refusesUnusableModelBaseUrl()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    ApiClient client;
    std::optional<ApiClient::Error> error;
    const auto list = [&client, &error](const QString& baseUrl) {
        ConfigManager::instance()->setValue(Keys::apiBaseUrl, baseUrl);
        error.reset();
        client.listModels(ApiClient::Connection::configured(), nullptr,
                          [&error](const ApiClient::Error& failure) { error = failure; });
    };

    list(QString());
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ApiClient::ErrorCode::BaseUrlMissing);

    for (const QString& baseUrl : {QStringLiteral("ftp://example.com/v1"),
                                   QStringLiteral("/v1"),
                                   QStringLiteral("not a url")}) {
        list(baseUrl);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, ApiClient::ErrorCode::BaseUrlInvalid);
    }
}

// An error body the endpoint wraps in a root JSON array still reaches the user.
void TestApiClient::reportsArrayWrappedFailure()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    OneShotServer server;
    QVERIFY(server.listen());
    server.serve(failureReply(
        "400 Bad Request",
        QByteArrayLiteral(
            R"([{"error":{"code":400,"message":"Please pass a valid API key","status":"INVALID_ARGUMENT"}}])")));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.port()));

    ApiClient client;
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    std::optional<ApiClient::Error> error;
    client.sendChatRequest(chatBody(), nullptr, nullptr,
                           [&error](const ApiClient::Error& failure) { error = failure; });

    QVERIFY(TestSupport::waitFor([&] { return finishedSpy.count() == 1; }, 5000));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ApiClient::ErrorCode::ServerMessage);
    QCOMPARE(error->detail, QStringLiteral("Please pass a valid API key"));
}

QTEST_MAIN(TestApiClient)
#include "tst_apiclient.moc"
