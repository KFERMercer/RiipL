#include <QtTest>

#include "TestSupport.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/document/DocumentSegmenter.h"
#include "core/translation/DocumentTranslator.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

#include <memory>

namespace {

// Answers chat-completions requests with one translated line per window line, so
// a run can be driven without a provider. A pending failure answers with the
// error the provider returns when it runs out of context.
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

    // Requests answered with a failure before one is answered normally.
    int failuresLeft = 0;
    // Requests answered with a window of the wrong shape before one is answered
    // normally.
    int malformedLeft = 0;
    int requests = 0;
    QStringList prompts;
    QVector<QJsonObject> windows;

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
            if (buffer->size() < headerEnd + 4 + length)
                return;

            const QByteArray body = buffer->mid(headerEnd + 4, length);
            const QString prompt = TestSupport::requestUserPrompt(body);
            ++requests;
            prompts.append(prompt);
            windows.append(TestSupport::documentWindowIn(prompt));
            const QByteArray answer = failuresLeft > 0 ? failure() : translationOf(prompt);
            if (failuresLeft > 0)
                --failuresLeft;
            else if (malformedLeft > 0)
                --malformedLeft;
            socket->write(answer);
            socket->flush();
            socket->disconnectFromHost();
        });
    }

    QByteArray failure() const
    {
        const QByteArray body =
            R"({"error":{"code":500,"message":"Context size has been exceeded."}})";
        return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n"
               "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    }

    // One JSON entry per window line, keyed from 1 on. A pending malformed answer
    // loses an entry, which breaks the mapping without failing the request.
    QByteArray translationOf(const QString& prompt) const
    {
        const QJsonObject window = TestSupport::documentWindowIn(prompt);
        QJsonObject answer;
        for (auto it = window.constBegin(); it != window.constEnd(); ++it)
            answer.insert(it.key(), QStringLiteral("译文 %1").arg(it.value().toString()));
        if (malformedLeft > 0 && !answer.isEmpty())
            answer.remove(answer.keys().constLast());

        const QJsonObject choice{
            {QStringLiteral("message"),
             QJsonObject{{QStringLiteral("content"),
                          QString::fromUtf8(QJsonDocument(answer).toJson(QJsonDocument::Compact))}}},
            {QStringLiteral("finish_reason"), QStringLiteral("stop")}
        };
        const QByteArray payload = QJsonDocument(
            QJsonObject{{QStringLiteral("choices"), QJsonArray{choice}}}).toJson(QJsonDocument::Compact);
        return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
            + QByteArray::number(payload.size()) + "\r\n\r\n" + payload;
    }

    QTcpServer m_server;
};

// Document of \p windows windows holding \p linesPerWindow distinct lines of
// four words each, so the word limits split it exactly where it is asked to.
QVector<DocumentWindow> document(int windows, int linesPerWindow)
{
    QStringList lines;
    for (int window = 0; window < windows; ++window) {
        for (int line = 0; line < linesPerWindow; ++line)
            lines << QStringLiteral("window %1 line %2").arg(window).arg(line);
        lines << QString();
    }
    return DocumentSegmenter::partition(lines.join(QLatin1Char('\n')),
                                        linesPerWindow * 4, linesPerWindow * 4 + 4);
}

} // namespace

class TestDocumentTranslator : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void translatesEveryWindowInOrder();
    void resendsTheSamePromptOnRetry();
    void retriesAnswerOfTheWrongShape();
    void failsOnceMalformedAnswersAreExhausted();
    void reportsTheLineCountOfARepeatedWindow();
    void failsOnceAttemptsAreExhausted();
    void retriesAgainOnTheNextRun();
    void retriesAsOftenAsConfigured();
    void usesTheDefaultRetryCount();
    void stopsDuringResultDelivery();
    void stopsWhileARetryIsPending();
    void finishesEmptyInputWithoutRequest();

private:
    StubApi m_api;
};

void TestDocumentTranslator::init()
{
    m_api.failuresLeft = 0;
    m_api.malformedLeft = 0;
    m_api.requests = 0;
    m_api.prompts.clear();
    m_api.windows.clear();

    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    ConfigManager* config = ConfigManager::instance();
    config->setValue(Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(m_api.port()));
    config->setValue(Keys::apiStream, false);
    // Two retries keep the counting of the cases below at three requests.
    config->setValue(Keys::documentRetryCount, 2);
}

void TestDocumentTranslator::translatesEveryWindowInOrder()
{
    const QVector<DocumentWindow> windows = document(3, 4);
    QCOMPARE(windows.size(), 3);

    TranslationContext context;
    context.sourceLang = QStringLiteral("en");
    context.targetLang = QStringLiteral("zh");

    DocumentTranslator translator;
    QString finished;
    QVector<QPair<int, int>> progress;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });
    connect(&translator, &DocumentTranslator::progressChanged, this,
            [&progress](int completed, int total) { progress.append({completed, total}); });

    translator.start(windows, context);
    QTRY_COMPARE(progress.size(), 4);
    QVERIFY(!finished.isEmpty());
    QCOMPARE(m_api.requests, 3);
    QCOMPARE(progress.first(), qMakePair(0, 3));
    QCOMPARE(progress.last(), qMakePair(3, 3));

    // Every line of the document is answered on its own, blanks included.
    const QStringList source = DocumentSegmenter::assemble(windows, {}).split(QLatin1Char('\n'));
    const QStringList result = finished.split(QLatin1Char('\n'));
    QCOMPARE(result.size(), source.size());
    for (int line = 0; line < source.size(); ++line) {
        if (source.at(line).isEmpty())
            QVERIFY2(result.at(line).isEmpty(), qPrintable(result.at(line)));
        else
            QCOMPARE(result.at(line), QStringLiteral("译文 %1").arg(source.at(line)));
    }

    // Every request carries its window as numbered JSON, and only the first one
    // has no preceding segment.
    QCOMPARE(m_api.windows.size(), 3);
    const QJsonObject first = m_api.windows.first();
    QCOMPARE(first.keys(), QStringList({QStringLiteral("1"), QStringLiteral("2"),
                                        QStringLiteral("3"), QStringLiteral("4")}));
    QCOMPARE(first.value(QStringLiteral("1")).toString(), QStringLiteral("window 0 line 0"));
    QCOMPARE(first.value(QStringLiteral("4")).toString(), QStringLiteral("window 0 line 3"));
    QVERIFY(m_api.prompts.first().contains(QStringLiteral("None")));
    QVERIFY(m_api.prompts.first().contains(QStringLiteral("window 1 line 0")));
}

void TestDocumentTranslator::resendsTheSamePromptOnRetry()
{
    m_api.failuresLeft = 1;

    const QVector<DocumentWindow> windows = document(2, 3);
    TranslationContext context;
    DocumentTranslator translator;
    QString finished;
    int restarts = 0;
    connect(&translator, &DocumentTranslator::windowRestarted, this, [&restarts]() { ++restarts; });
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    QCOMPARE(restarts, 1);
    QCOMPARE(m_api.requests, 3);

    // A retry repeats the request as it was, neighbouring segments included.
    QCOMPARE(m_api.prompts.at(1), m_api.prompts.at(0));
    QVERIFY(m_api.prompts.at(0).contains(QStringLiteral("window 1 line 0")));
}

void TestDocumentTranslator::retriesAnswerOfTheWrongShape()
{
    m_api.malformedLeft = 1;

    const QVector<DocumentWindow> windows = document(1, 3);
    TranslationContext context;
    DocumentTranslator translator;
    QString finished;
    int restarts = 0;
    connect(&translator, &DocumentTranslator::windowRestarted, this, [&restarts]() { ++restarts; });
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);

    // An answer missing a key is sent again, and the retry repeats the request.
    QCOMPARE(restarts, 1);
    QCOMPARE(m_api.requests, 2);
    QCOMPARE(m_api.prompts.at(1), m_api.prompts.at(0));
    QCOMPARE(finished, QStringLiteral("译文 window 0 line 0\n"
                                      "译文 window 0 line 1\n"
                                      "译文 window 0 line 2\n"));
}

void TestDocumentTranslator::failsOnceMalformedAnswersAreExhausted()
{
    m_api.malformedLeft = 10;

    const QVector<DocumentWindow> windows = document(2, 3);
    TranslationContext context;
    DocumentTranslator translator;
    ApiClient::Error failure;
    bool reported = false;
    bool done = false;
    connect(&translator, &DocumentTranslator::failed, this, [&](const ApiClient::Error& error) {
        reported = true;
        failure = error;
    });
    connect(&translator, &DocumentTranslator::finished, this, [&done](const QString&) { done = true; });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(reported, 10000);

    // A wrong shape is retried like any other failed window, and reports the
    // line count the answer was expected to hold.
    QVERIFY(!done);
    QCOMPARE(m_api.requests, 3);
    QCOMPARE(failure.code, ApiClient::ErrorCode::LineCountMismatch);
    QCOMPARE(failure.detail, QStringLiteral("3"));
}

void TestDocumentTranslator::reportsTheLineCountOfARepeatedWindow()
{
    m_api.malformedLeft = 10;

    // Three equal lines collapse into one document line, so the window holds two
    // lines and the answer is keyed 1..2 however often a line occurs.
    const QVector<DocumentWindow> windows =
        DocumentSegmenter::partition(QStringLiteral("alpha\nalpha\nalpha\nbeta"));
    QCOMPARE(windows.size(), 1);
    QCOMPARE(windows.first().lineCount(), 2);

    TranslationContext context;
    DocumentTranslator translator;
    ApiClient::Error failure;
    bool reported = false;
    connect(&translator, &DocumentTranslator::failed, this, [&](const ApiClient::Error& error) {
        reported = true;
        failure = error;
    });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(reported, 10000);

    // The count the answer was expected to hold is the number of window lines.
    QCOMPARE(m_api.requests, 3);
    QCOMPARE(failure.code, ApiClient::ErrorCode::LineCountMismatch);
    QCOMPARE(failure.detail, QStringLiteral("2"));
}

void TestDocumentTranslator::failsOnceAttemptsAreExhausted()
{
    m_api.failuresLeft = 10;

    const QVector<DocumentWindow> windows = document(2, 3);
    TranslationContext context;
    DocumentTranslator translator;
    ApiClient::Error failure;
    bool reported = false;
    bool done = false;
    connect(&translator, &DocumentTranslator::failed, this, [&](const ApiClient::Error& error) {
        reported = true;
        failure = error;
    });
    connect(&translator, &DocumentTranslator::finished, this, [&done](const QString&) { done = true; });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(reported, 10000);
    QVERIFY(!done);
    QCOMPARE(m_api.requests, 3);
    QCOMPARE(failure.code, ApiClient::ErrorCode::ServerMessage);
    QVERIFY(failure.text().contains(QStringLiteral("Context size")));
}

// A second run after a failed one is entitled to its own attempts.
void TestDocumentTranslator::retriesAgainOnTheNextRun()
{
    m_api.failuresLeft = 10;

    const QVector<DocumentWindow> windows = document(1, 3);
    TranslationContext context;
    DocumentTranslator translator;
    int failures = 0;
    QString finished;
    connect(&translator, &DocumentTranslator::failed, this, [&failures](const ApiClient::Error&) {
        ++failures;
    });
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(failures == 1, 10000);
    QCOMPARE(m_api.requests, 3);

    m_api.failuresLeft = 1;
    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    QCOMPARE(failures, 1);
    QCOMPARE(m_api.requests, 5);
}

// The configured count is extra attempts, so a window is sent once more than that.
void TestDocumentTranslator::retriesAsOftenAsConfigured()
{
    const QVector<DocumentWindow> windows = document(1, 3);
    TranslationContext context;
    DocumentTranslator translator;
    int failures = 0;
    connect(&translator, &DocumentTranslator::failed, this,
            [&failures](const ApiClient::Error&) { ++failures; });

    m_api.failuresLeft = 10;
    ConfigManager::instance()->setValue(Keys::documentRetryCount, 0);
    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(failures == 1, 10000);
    QCOMPARE(m_api.requests, 1);

    m_api.failuresLeft = 10;
    ConfigManager::instance()->setValue(Keys::documentRetryCount, 2);
    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(failures == 2, 10000);
    QCOMPARE(m_api.requests, 4);
}

void TestDocumentTranslator::usesTheDefaultRetryCount()
{
    ConfigManager::instance()->removeValue(Keys::documentRetryCount);
    QCOMPARE(ConfigManager::instance()->intValue(Keys::documentRetryCount),
             Defaults::documentRetryCount);
    QCOMPARE(Defaults::documentRetryCount, 3);

    m_api.failuresLeft = 10;
    DocumentTranslator translator;
    int failures = 0;
    connect(&translator, &DocumentTranslator::failed, this,
            [&failures](const ApiClient::Error&) { ++failures; });

    translator.start(document(1, 3), TranslationContext());
    QTRY_VERIFY_WITH_TIMEOUT(failures == 1, 15000);
    QCOMPARE(m_api.requests, 4);
}

void TestDocumentTranslator::stopsDuringResultDelivery()
{
    const QVector<DocumentWindow> windows = document(3, 3);
    TranslationContext context;
    DocumentTranslator translator;
    bool stopped = false;
    QString partial;
    QString finished;
    connect(&translator, &DocumentTranslator::progressChanged, this,
            [&translator](int completed, int) {
                if (completed == 1)
                    translator.stop();
            });
    connect(&translator, &DocumentTranslator::stopped, this, [&stopped]() { stopped = true; });
    connect(&translator, &DocumentTranslator::windowTranslated, this,
            [&partial](const QString& text) { partial = text; });
    connect(&translator, &DocumentTranslator::finished, this, [&finished](const QString& text) {
        finished = text;
    });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(stopped, 10000);

    // The accepted window stays and the run does not go on.
    QVERIFY(!partial.isEmpty());
    QVERIFY(finished.isEmpty());
    QCOMPARE(m_api.requests, 1);
}

void TestDocumentTranslator::stopsWhileARetryIsPending()
{
    m_api.failuresLeft = 10;

    const QVector<DocumentWindow> windows = document(3, 3);
    TranslationContext context;
    DocumentTranslator translator;
    bool stopped = false;
    connect(&translator, &DocumentTranslator::windowRestarted, this,
            [&translator]() { translator.stop(); });
    connect(&translator, &DocumentTranslator::stopped, this, [&stopped]() { stopped = true; });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(stopped, 10000);

    // A stop that lands while the retry waits must keep the request from going
    // out once the pause is over.
    QTest::qWait(1500);
    QCOMPARE(m_api.requests, 1);
}

void TestDocumentTranslator::finishesEmptyInputWithoutRequest()
{
    DocumentTranslator translator;
    QString finished = QStringLiteral("not yet");
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start({}, TranslationContext());
    QCOMPARE(finished, QString());
    QCOMPARE(m_api.requests, 0);
}

QTEST_MAIN(TestDocumentTranslator)
#include "tst_documenttranslator.moc"
