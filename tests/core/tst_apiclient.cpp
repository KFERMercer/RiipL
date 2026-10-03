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
                m_requests.append(socket->readAll());
                socket->write(m_replies.isEmpty() ? QByteArray() : m_replies.takeFirst());
                socket->flush();
                socket->disconnectFromHost();
            });
        });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost); }
    quint16 port() const { return m_server.serverPort(); }

    void serve(const QByteArray& response) { m_replies.append(response); }

    // Raw bytes of every request that reached the server.
    const QList<QByteArray>& requests() const { return m_requests; }

private:
    QTcpServer m_server;
    QList<QByteArray> m_replies;
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
    void parsesModelIds();
    void listsModels();
    void listsModelsFromGivenValues();
    void reportsModelListFailure();
    void dropsOversizedModelList();
    void refusesUnusableModelBaseUrl();
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

// A body without a model array names nothing, which is not the same as an
// endpoint that reports no model.
void TestApiClient::parsesModelIds()
{
    QVERIFY(!ApiClient::parseModelIds(QByteArrayLiteral("not json")).has_value());
    QVERIFY(!ApiClient::parseModelIds(QByteArrayLiteral(R"({"object":"list"})")).has_value());
    QVERIFY(!ApiClient::parseModelIds(QByteArrayLiteral(R"({"data":"none"})")).has_value());

    // Entries without an id are skipped; the order the endpoint reports is kept.
    const std::optional<QStringList> ids = ApiClient::parseModelIds(
        QByteArrayLiteral(R"({"data":[{"id":"gpt-4o-mini"},{"id":""},{},"o3-mini",{"id":"gpt-4.1-mini"}]})"));
    QVERIFY(ids.has_value());
    QCOMPARE(*ids, QStringList({QStringLiteral("gpt-4o-mini"), QStringLiteral("gpt-4.1-mini")}));

    const std::optional<QStringList> empty = ApiClient::parseModelIds(QByteArrayLiteral(R"({"data":[]})"));
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
    // The listing is not a translation, so it leaves the busy state alone.
    QSignalSpy finishedSpy(&client, &ApiClient::requestFinished);
    QStringList ids;
    std::optional<ApiClient::Error> error;
    client.listModels(ApiClient::Connection::configured(),
                      [&ids](const QStringList& models) { ids = models; },
                      [&error](const ApiClient::Error& failure) { error = failure; });

    QTRY_COMPARE_WITH_TIMEOUT(ids.size(), 2, 5000);
    QVERIFY2(!error.has_value(), "the listing must not report a failure");
    QCOMPARE(ids, QStringList({QStringLiteral("gpt-4o-mini"), QStringLiteral("o3-mini")}));
    QCOMPARE(finishedSpy.count(), 0);

    QCOMPARE(server.requests().size(), 1);
    const QByteArray request = server.requests().first();
    QCOMPARE(request.split('\n').first().trimmed(), QByteArrayLiteral("GET /v1/models HTTP/1.1"));
    QVERIFY(request.toLower().contains("authorization: bearer secret"));
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
    QStringList ids;
    std::optional<ApiClient::Error> error;
    client.listModels(ApiClient::Connection::fromValues(values),
                      [&ids](const QStringList& models) { ids = models; },
                      [&error](const ApiClient::Error& failure) { error = failure; });

    QTRY_COMPARE_WITH_TIMEOUT(ids.size(), 1, 5000);
    QVERIFY2(!error.has_value(), "the given settings must carry the request");
    QCOMPARE(ids, QStringList({QStringLiteral("local-model")}));

    QCOMPARE(server.requests().size(), 1);
    const QByteArray request = server.requests().first();
    QCOMPARE(request.split('\n').first().trimmed(), QByteArrayLiteral("GET /v1/models HTTP/1.1"));
    const QByteArray lowered = request.toLower();
    QVERIFY(lowered.contains("authorization: bearer given-key"));
    QVERIFY(lowered.contains("x-probe: 1"));
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
    QStringList ids;
    std::optional<ApiClient::Error> error;
    client.listModels(ApiClient::Connection::configured(),
                      [&ids](const QStringList& models) { ids = models; },
                      [&error](const ApiClient::Error& failure) { error = failure; });

    QTRY_VERIFY_WITH_TIMEOUT(error.has_value(), 5000);
    QCOMPARE(error->code, ApiClient::ErrorCode::ServerMessage);
    QCOMPARE(error->detail, QStringLiteral("invalid key"));
    QVERIFY(ids.isEmpty());
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
    QStringList ids;
    std::optional<ApiClient::Error> error;
    client.listModels(ApiClient::Connection::configured(),
                      [&ids](const QStringList& models) { ids = models; },
                      [&error](const ApiClient::Error& failure) { error = failure; });

    QTRY_VERIFY_WITH_TIMEOUT(error.has_value(), 15000);
    QCOMPARE(error->code, ApiClient::ErrorCode::ResponseTooLarge);
    QVERIFY(ids.isEmpty());
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

QTEST_MAIN(TestApiClient)
#include "tst_apiclient.moc"
