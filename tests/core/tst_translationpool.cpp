#include <QtTest>

#include "TestSupport.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/translation/DocumentTranslator.h"
#include "core/translation/TranslationEngine.h"
#include "core/translation/TranslationPool.h"
#include "core/translation/YoloTranslator.h"

#include <QDir>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>
#include <memory>
#include <utility>

namespace {

// Answers chat-completions requests, holding each one back until the test
// releases it, so the order and the number in flight are observable.
class StubApi
{
public:
    StubApi()
    {
        m_server.listen(QHostAddress::LocalHost);
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() {
            accept(m_server.nextPendingConnection());
        });
    }

    quint16 port() const { return m_server.serverPort(); }

    bool hold = true;
    // Requests answered with a failure before one is answered normally.
    int failuresLeft = 0;
    int pending = 0;
    int peakPending = 0;
    int requests = 0;
    QStringList prompts;

    // Answers every held request and lets the ones behind it through.
    void releaseAll()
    {
        hold = false;
        const QVector<QTcpSocket*> held = std::exchange(m_held, {});
        for (QTcpSocket* socket : held) {
            const bool failure = failuresLeft > 0;
            if (failure)
                --failuresLeft;
            answer(socket, failure);
        }
    }

    // Answers the request held longest, with the outcome the test asks for.
    void releaseNext(bool failure)
    {
        if (m_held.isEmpty())
            return;
        answer(m_held.takeFirst(), failure);
    }

    void reset()
    {
        hold = true;
        failuresLeft = 0;
        pending = 0;
        peakPending = 0;
        requests = 0;
        prompts.clear();
        m_held.clear();
    }

private:
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
            if (request.size() < headerEnd + 4 + length)
                return;

            ++requests;
            prompts.append(TestSupport::requestUserPrompt(
                request.mid(headerEnd + 4, length).toUtf8()));
            ++pending;
            peakPending = (std::max)(peakPending, pending);
            if (hold)
                m_held.append(socket);
            else {
                const bool failure = failuresLeft > 0;
                if (failure)
                    --failuresLeft;
                answer(socket, failure);
            }
        });
    }

    void answer(QTcpSocket* socket, bool failure)
    {
        --pending;
        const QByteArray payload = failure
            ? QByteArrayLiteral(R"({"error":{"message":"busy"}})")
            : QByteArrayLiteral(R"({"choices":[{"message":{"content":"ok"}}]})");
        const QByteArray status = failure ? QByteArrayLiteral("500 Internal Server Error")
                                          : QByteArrayLiteral("200 OK");
        socket->write("HTTP/1.1 " + status
                      + "\r\nContent-Type: application/json\r\nContent-Length: "
                      + QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QVector<QTcpSocket*> m_held;
};

QJsonObject chatBody(const QString& prompt)
{
    return QJsonObject{
        {QStringLiteral("messages"),
         QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), prompt}}}}
    };
}

}

class TestTranslationPool : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void interactiveRequestPassesQueuedDocuments();
    void ordersTheCallersPriorities();
    void reportsWhenItCarriesRequests();
    void reportsEveryFailedRequest();
    void keepsRequestsWithinTheConfiguredConcurrency();
    void halvesTheConcurrencyOnFailureAndStepsBackOnSuccess();
    void stepsBackOnlyAfterTwoSuccessesAtTheFilledWidth();
    void halvesAgainFromTheSteppedBackWidth();
    void cancelReportsNothing();
    void cancellingARunningRequestFreesItsConnection();
    void cancellingASettledJobLeavesTheRunningOneAlone();

private:
    StubApi m_api;
};

void TestTranslationPool::init()
{
    m_api.reset();
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    ConfigManager* config = ConfigManager::instance();
    config->setValue(Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(m_api.port()));
    config->setValue(Keys::apiStream, false);
}

// A request the user is waiting for takes the connection ahead of the document
// windows queued before it.
void TestTranslationPool::interactiveRequestPassesQueuedDocuments()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 1);
    TranslationPool pool;

    QStringList answered;
    const auto enqueue = [&pool, &answered](const QString& name, int priority) {
        pool.enqueue(chatBody(name), priority,
                     [&answered, name](const QString&) { answered.append(name); },
                     {}, [&answered, name](const ApiClient::Error&) { answered.append(name + QLatin1Char('!')); });
    };

    enqueue(QStringLiteral("doc1"), DocumentPriority::window);
    QTRY_COMPARE(m_api.requests, 1);

    // The one connection is taken, so both wait for a slot.
    enqueue(QStringLiteral("doc2"), DocumentPriority::window);
    enqueue(QStringLiteral("interactive"), TranslationPriority::interactive);
    QTest::qWait(50);
    QCOMPARE(m_api.requests, 1);

    m_api.releaseAll();
    QTRY_COMPARE(answered.size(), 3);
    QCOMPARE(m_api.prompts, QStringList({QStringLiteral("doc1"), QStringLiteral("interactive"),
                                         QStringLiteral("doc2")}));
}

// The callers' numbers hold the ladder the app relies on: a retry waits ahead of
// a first attempt, a clipboard request ahead of every document request, and an
// interactive request ahead of every clipboard request.
void TestTranslationPool::ordersTheCallersPriorities()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 1);
    TranslationPool pool;

    const auto enqueue = [&pool](const QString& name, int priority) {
        pool.enqueue(chatBody(name), priority, {}, {}, {});
    };

    // Occupies the one connection while the rest queue in an order the priorities
    // have to override.
    enqueue(QStringLiteral("running"), DocumentPriority::window);
    QTRY_COMPARE(m_api.requests, 1);
    enqueue(QStringLiteral("window"), DocumentPriority::window);
    enqueue(QStringLiteral("windowRetry"), DocumentPriority::windowRetry);
    enqueue(QStringLiteral("clipboard"), YoloPriority::clipboard);
    enqueue(QStringLiteral("clipboardRetry"), YoloPriority::clipboardRetry);
    enqueue(QStringLiteral("interactive"), TranslationPriority::interactive);
    enqueue(QStringLiteral("interactiveRetry"), TranslationPriority::interactiveRetry);

    m_api.releaseAll();
    QTRY_COMPARE(m_api.prompts.size(), 7);
    QCOMPARE(m_api.prompts, QStringList({QStringLiteral("running"), QStringLiteral("interactiveRetry"),
                                         QStringLiteral("interactive"), QStringLiteral("clipboardRetry"),
                                         QStringLiteral("clipboard"), QStringLiteral("windowRetry"),
                                         QStringLiteral("window")}));
}

// The pool announces the first request it takes and the last one it answers, and
// stays quiet while it keeps carrying requests.
void TestTranslationPool::reportsWhenItCarriesRequests()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 1);
    TranslationPool pool;
    QSignalSpy busy(&pool, &TranslationPool::busyChanged);
    QVERIFY(!pool.isBusy());

    pool.enqueue(chatBody(QStringLiteral("doc1")), DocumentPriority::window, {}, {}, {});
    QVERIFY(pool.isBusy());
    QCOMPARE(busy.count(), 1);
    QCOMPARE(busy.at(0).at(0).toBool(), true);

    QTRY_COMPARE(m_api.requests, 1);
    QTest::qWait(50);
    QCOMPARE(busy.count(), 1);

    m_api.releaseAll();
    QTRY_VERIFY(!pool.isBusy());
    QCOMPARE(busy.count(), 2);
    QCOMPARE(busy.at(1).at(0).toBool(), false);

    QTest::qWait(50);
    QCOMPARE(busy.count(), 2);
}

// Every failed request is announced once, and a success announces nothing.
void TestTranslationPool::reportsEveryFailedRequest()
{
    TranslationPool pool;
    QSignalSpy failed(&pool, &TranslationPool::requestFailed);

    pool.enqueue(chatBody(QStringLiteral("ok")), DocumentPriority::window, {}, {}, {});
    QTRY_COMPARE(m_api.requests, 1);
    m_api.releaseNext(false);
    QTest::qWait(50);
    QCOMPARE(failed.count(), 0);

    pool.enqueue(chatBody(QStringLiteral("bad")), DocumentPriority::window, {}, {}, {});
    QTRY_COMPARE(m_api.requests, 2);
    m_api.releaseNext(true);
    QTRY_COMPARE(failed.count(), 1);

    QTest::qWait(50);
    QCOMPARE(failed.count(), 1);
}

// The configured count is how many requests the pool carries at once.
void TestTranslationPool::keepsRequestsWithinTheConfiguredConcurrency()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 2);
    TranslationPool pool;

    for (int index = 0; index < 5; ++index)
        pool.enqueue(chatBody(QStringLiteral("doc%1").arg(index)), DocumentPriority::window, {}, {}, {});

    QTRY_COMPARE(m_api.requests, 2);
    QTest::qWait(50);
    QCOMPARE(m_api.requests, 2);
    QCOMPARE(m_api.peakPending, 2);

    m_api.releaseAll();
    QTRY_COMPARE(m_api.requests, 5);
    QCOMPARE(m_api.peakPending, 2);
}

// A failed request halves the connections the pool uses, and two successes at the
// reduced width give one halving back rather than the configured count.
void TestTranslationPool::halvesTheConcurrencyOnFailureAndStepsBackOnSuccess()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 4);
    TranslationPool pool;

    for (int index = 0; index < 4; ++index)
        pool.enqueue(chatBody(QStringLiteral("doc%1").arg(index)), DocumentPriority::window, {}, {}, {});
    QTRY_COMPARE(m_api.requests, 4);

    // Four failures take the four connections down to one.
    for (int index = 0; index < 4; ++index)
        m_api.releaseNext(true);
    QTest::qWait(50);

    // One connection is left, so a burst of four puts one request on the wire.
    for (int index = 0; index < 4; ++index)
        pool.enqueue(chatBody(QStringLiteral("later%1").arg(index)), DocumentPriority::window, {}, {}, {});
    QTest::qWait(50);
    QCOMPARE(m_api.requests, 5);

    // The first success leaves the width at one; the freed connection takes the
    // next queued request instead.
    m_api.releaseNext(false);
    QTest::qWait(50);
    QCOMPARE(m_api.requests, 6);

    // The second success gives one halving back, which is two connections, so the
    // queue behind it fills both.
    m_api.releaseNext(false);
    QTRY_COMPARE(m_api.pending, 2);
    QCOMPARE(m_api.requests, 8);
}

// The back-off gives a halving back only once the reduced width has been filled
// again and two successes have landed at that width.
void TestTranslationPool::stepsBackOnlyAfterTwoSuccessesAtTheFilledWidth()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 4);
    TranslationPool pool;

    for (int index = 0; index < 4; ++index)
        pool.enqueue(chatBody(QStringLiteral("doc%1").arg(index)), DocumentPriority::window, {}, {}, {});
    QTRY_COMPARE(m_api.requests, 4);

    // One failure takes the four connections down to two.
    m_api.releaseNext(true);
    QTest::qWait(50);

    // The successes beside the failure count for nothing: the failure dropped the
    // filled width, and no request went out afterwards to fill it again.
    for (int index = 0; index < 3; ++index)
        m_api.releaseNext(false);
    QTest::qWait(50);

    // Two connections is still the width, so only two of three go out.
    for (int index = 0; index < 3; ++index)
        pool.enqueue(chatBody(QStringLiteral("later%1").arg(index)), DocumentPriority::window, {}, {}, {});
    QTest::qWait(50);
    QCOMPARE(m_api.requests, 6);

    // The first success at the filled width leaves it at two.
    m_api.releaseNext(false);
    QTest::qWait(50);
    QCOMPARE(m_api.pending, 2);

    // The second steps the back-off back to the configured four, and the queue
    // behind it fills all four.
    m_api.releaseNext(false);
    for (int index = 0; index < 4; ++index)
        pool.enqueue(chatBody(QStringLiteral("last%1").arg(index)), DocumentPriority::window, {}, {}, {});
    QTRY_COMPARE(m_api.pending, 4);
}

// A failure that follows a step back halves the width from where it stands.
void TestTranslationPool::halvesAgainFromTheSteppedBackWidth()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 8);
    TranslationPool pool;

    // Two failures take the eight connections down to two.
    for (int index = 0; index < 2; ++index)
        pool.enqueue(chatBody(QStringLiteral("doc%1").arg(index)), DocumentPriority::window, {}, {}, {});
    QTRY_COMPARE(m_api.requests, 2);
    m_api.releaseNext(true);
    QTest::qWait(50);
    m_api.releaseNext(true);
    QTest::qWait(50);

    // Two successes at the width of two step it back to four.
    for (int index = 0; index < 2; ++index)
        pool.enqueue(chatBody(QStringLiteral("step%1").arg(index)), DocumentPriority::window, {}, {}, {});
    QTRY_COMPARE(m_api.requests, 4);
    m_api.releaseNext(false);
    QTest::qWait(50);
    m_api.releaseNext(false);
    QTest::qWait(50);

    // The next failure halves that width, so four queued requests put two on the
    // wire rather than one or four.
    pool.enqueue(chatBody(QStringLiteral("fail")), DocumentPriority::window, {}, {}, {});
    QTRY_COMPARE(m_api.requests, 5);
    m_api.releaseNext(true);
    QTest::qWait(50);

    for (int index = 0; index < 4; ++index)
        pool.enqueue(chatBody(QStringLiteral("after%1").arg(index)), DocumentPriority::window, {}, {}, {});
    QTest::qWait(50);
    QCOMPARE(m_api.pending, 2);
}

// A cancelled request is dropped, queued or running, without reaching its
// callbacks.
void TestTranslationPool::cancelReportsNothing()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 1);
    TranslationPool pool;

    bool reported = false;
    const auto callbacks = [&reported]() {
        return std::pair{[&reported](const QString&) { reported = true; },
                         [&reported](const ApiClient::Error&) { reported = true; }};
    };

    auto first = callbacks();
    const TranslationPool::JobId running = pool.enqueue(chatBody(QStringLiteral("doc1")),
                                                        DocumentPriority::window,
                                                        first.first, {}, first.second);
    QTRY_COMPARE(m_api.requests, 1);

    auto second = callbacks();
    const TranslationPool::JobId queued = pool.enqueue(chatBody(QStringLiteral("doc2")),
                                                       DocumentPriority::window,
                                                       second.first, {}, second.second);
    pool.cancel(queued);
    pool.cancel(running);

    m_api.releaseAll();
    QTest::qWait(100);
    QVERIFY(!reported);
    QCOMPARE(m_api.requests, 1);
}

// A cancelled running request gives its connection back to the queue.
void TestTranslationPool::cancellingARunningRequestFreesItsConnection()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 1);
    TranslationPool pool;

    const TranslationPool::JobId running = pool.enqueue(chatBody(QStringLiteral("doc1")),
                                                        DocumentPriority::window, {}, {}, {});
    QTRY_COMPARE(m_api.requests, 1);

    QStringList answered;
    pool.enqueue(chatBody(QStringLiteral("doc2")), DocumentPriority::window,
                 [&answered](const QString&) { answered.append(QStringLiteral("doc2")); }, {}, {});

    pool.cancel(running);
    QTRY_COMPARE(m_api.requests, 2);

    m_api.releaseAll();
    QTRY_COMPARE(answered.size(), 1);
    QCOMPARE(m_api.prompts, QStringList({QStringLiteral("doc1"), QStringLiteral("doc2")}));
}

// A job id that has already been answered no longer names the request now running
// on the connection it used.
void TestTranslationPool::cancellingASettledJobLeavesTheRunningOneAlone()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 1);
    TranslationPool pool;

    QStringList answered;
    const TranslationPool::JobId settled = pool.enqueue(
        chatBody(QStringLiteral("doc1")), DocumentPriority::window,
        [&answered](const QString&) { answered.append(QStringLiteral("doc1")); }, {}, {});
    QTRY_COMPARE(m_api.requests, 1);
    m_api.releaseNext(false);
    QTRY_COMPARE(answered.size(), 1);

    pool.enqueue(chatBody(QStringLiteral("doc2")), DocumentPriority::window,
                 [&answered](const QString&) { answered.append(QStringLiteral("doc2")); }, {}, {});
    QTRY_COMPARE(m_api.requests, 2);

    pool.cancel(settled);
    m_api.releaseAll();
    QTRY_COMPARE(answered.size(), 2);
    QCOMPARE(answered, QStringList({QStringLiteral("doc1"), QStringLiteral("doc2")}));
}

QTEST_MAIN(TestTranslationPool)
#include "tst_translationpool.moc"
