#include <QtTest>

#include "TestSupport.h"
#include "core/config/Defaults.h"
#include "core/config/ConfigManager.h"
#include "core/network/ApiClient.h"

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
                socket->readAll();
                socket->write(m_replies.isEmpty() ? QByteArray() : m_replies.takeFirst());
                socket->flush();
                socket->disconnectFromHost();
            });
        });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost); }
    quint16 port() const { return m_server.serverPort(); }

    void serve(const QByteArray& response) { m_replies.append(response); }

private:
    QTcpServer m_server;
    QList<QByteArray> m_replies;
};

QByteArray jsonReply(const QByteArray& body)
{
    return QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
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

QJsonObject chatBody()
{
    QJsonObject body;
    body.insert(QStringLiteral("stream"), false);
    return body;
}

}

class TestApiClient : public QObject
{
    Q_OBJECT

private slots:
    void normalizesBaseUrl();
    void requestsDerivedEndpoint();
    void parsesCustomHeaderLines();
    void refusesUnusableBaseUrl();
    void reportsIdleBeforeDelivering();
    void keepsResultWhenHandlerStartsNextRequest();
    void reportsIdleTimeout();
    void readsTrailingStreamFrame();
    void rejectsStreamWithoutContent();
    void dropsOversizedResponse();
};

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
    QJsonObject body;
    body.insert(QStringLiteral("stream"), false);
    client.sendChatRequest(body,
                           [&](const QString& text) { result = text; },
                           nullptr,
                           [&](const ApiClient::Error& failure) { error = failure; });

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QVERIFY2(!error.has_value(), "the request must not report a failure");
    QCOMPARE(result, QStringLiteral("ok"));
    QCOMPARE(requestLine, QStringLiteral("POST /v1/chat/completions HTTP/1.1"));
    QCOMPARE(authorization, QByteArrayLiteral("Bearer secret"));
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

    QTRY_COMPARE_WITH_TIMEOUT(order.size(), 2, 5000);
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

    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 2, 5000);
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

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
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

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QVERIFY2(!error.has_value(), "the trailing frame must be read");
    QCOMPARE(result, QStringLiteral("Hello"));
}

// A stream without content is a failure, not an empty translation.
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

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ApiClient::ErrorCode::InvalidResponse);
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

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 15000);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ApiClient::ErrorCode::ResponseTooLarge);
}

QTEST_MAIN(TestApiClient)
#include "tst_apiclient.moc"
