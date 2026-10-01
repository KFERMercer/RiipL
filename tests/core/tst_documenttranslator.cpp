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
#include <QTimer>

#include <memory>

namespace {

// Answers chat-completions requests with one translated line per window line, so
// a run can be driven without a provider. A pending failure answers with the
// error the provider returns when it runs out of context. Answers can be held
// back, which lets a run's requests overlap and be counted while they wait.
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
    // Requests whose user prompt holds this text are answered with a failure; a
    // specific window fails whatever order the requests arrive in. The window
    // fails once, so its retry is answered normally. The text must be the
    // window's own JSON key, since the neighbouring segments are plain text.
    QString failingPrompt;
    bool failingPromptSpent = false;
    // Delay before a request is answered: the delay of the first marker the prompt
    // holds wins, otherwise \p answerDelayMs. A marker is the window's own JSON
    // key, since the neighbouring segments are plain text.
    QVector<QPair<QString, int>> answerDelays;
    // Requests answered with a window of the wrong shape before one is answered
    // normally.
    int malformedLeft = 0;
    int requests = 0;
    // Pause between reading a request and answering it.
    int answerDelayMs = 0;
    // Requests read but not yet answered, and the most that were pending at once.
    int pending = 0;
    int peakPending = 0;
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
            ++pending;
            peakPending = qMax(peakPending, pending);
            // The delayed failure targets one window; a failure counter is spent
            // by whichever request arrives first.
            const bool named = !failingPrompt.isEmpty() && prompt.contains(failingPrompt);
            const bool failing = (named && !failingPromptSpent)
                || (failingPrompt.isEmpty() && failuresLeft > 0);
            if (named && !failingPromptSpent)
                failingPromptSpent = true;
            const QByteArray answer = failing ? failure() : translationOf(prompt);
            if (failing && !named && failuresLeft > 0)
                --failuresLeft;
            if (!failing && malformedLeft > 0)
                --malformedLeft;
            // A named failure answers ahead of the requests beside it, which
            // keeps them on the wire when the run gives up. A marker delay lets a
            // test place one window's answer before or after the others.
            int delay = answerDelayMs;
            for (const QPair<QString, int>& marker : answerDelays) {
                if (prompt.contains(marker.first)) {
                    delay = marker.second;
                    break;
                }
            }
            QTimer::singleShot(delay, socket, [this, socket, answer]() {
                --pending;
                socket->write(answer);
                socket->flush();
                socket->disconnectFromHost();
            });
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

// Document of \p windows windows holding \p linesPerWindow distinct lines each,
// sized so a window holds exactly the lines it is asked to.
QVector<DocumentWindow> document(int windows, int linesPerWindow)
{
    QStringList lines;
    for (int window = 0; window < windows; ++window) {
        for (int line = 0; line < linesPerWindow; ++line)
            lines << QStringLiteral("window %1 line %2").arg(window).arg(line);
        lines << QString();
    }
    // Every line of a window is \p chars long, so a window holds \p
    // linesPerWindow of them, separated by the newlines between them.
    const int chars = QStringLiteral("window 0 line 0").size();
    return DocumentSegmenter::partition(lines.join(QLatin1Char('\n')),
                                        chars * linesPerWindow + linesPerWindow - 1,
                                        linesPerWindow);
}

} // namespace

class TestDocumentTranslator : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void translatesEveryWindowInOrder();
    void sendsWindowsUpToTheConfiguredConcurrency();
    void capsConcurrencyAfterTheLastWindow();
    void runsSerialWhenConcurrencyIsOff();
    void runsSerialAtOneRequest();
    void reassemblesDocumentsFromOverlappingAnswers();
    void showsAnswersWhereTheyBelongWhateverTheirOrder();
    void retriesOneWindowWhileOthersAreInFlight();
    void stopsEveryRequestInFlight();
    void dropsInFlightRequestsWhenAWindowGivesUp();
    void reusesTheSamePoolForASecondRun();
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
    m_api.answerDelayMs = 0;
    m_api.pending = 0;
    m_api.peakPending = 0;
    m_api.failingPrompt.clear();
    m_api.failingPromptSpent = false;
    m_api.answerDelays.clear();
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

// The configured count is how many windows a run sends at once.
void TestDocumentTranslator::sendsWindowsUpToTheConfiguredConcurrency()
{
    m_api.answerDelayMs = 50;
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 3);

    const QVector<DocumentWindow> windows = document(6, 3);
    QCOMPARE(windows.size(), 6);

    TranslationContext context;
    DocumentTranslator translator;
    QString finished;
    QVector<int> completed;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });
    connect(&translator, &DocumentTranslator::progressChanged, this,
            [&completed](int count, int) { completed.append(count); });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 20000);

    // Three requests overlap, and the run never exceeds the limit.
    QCOMPARE(m_api.peakPending, 3);
    QCOMPARE(m_api.requests, 6);
    QCOMPARE(completed.size(), 7);
    QCOMPARE(completed.first(), 0);
    QCOMPARE(completed.last(), 6);
    // Completion order follows the answers, so the document is rebuilt by window
    // number rather than by arrival.
    QCOMPARE(finished.split(QLatin1Char('\n')).size(),
             DocumentSegmenter::assemble(windows, {}).split(QLatin1Char('\n')).size());
    QVERIFY(finished.startsWith(QStringLiteral("译文 window 0 line 0")));
}

// A run of fewer windows than the limit never leaves a request idle.
void TestDocumentTranslator::capsConcurrencyAfterTheLastWindow()
{
    m_api.answerDelayMs = 50;
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 16);

    const QVector<DocumentWindow> windows = document(3, 3);
    TranslationContext context;
    DocumentTranslator translator;
    QString finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);

    // Only the three windows there are go out, however high the limit is.
    QCOMPARE(m_api.requests, 3);
    QCOMPARE(m_api.peakPending, 3);
}

// Turning the switch off keeps one request at a time.
void TestDocumentTranslator::runsSerialWhenConcurrencyIsOff()
{
    m_api.answerDelayMs = 20;
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 8);
    ConfigManager::instance()->setValue(Keys::documentConcurrent, false);

    const QVector<DocumentWindow> windows = document(4, 3);
    TranslationContext context;
    DocumentTranslator translator;
    QString finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);

    QCOMPARE(m_api.requests, 4);
    QCOMPARE(m_api.peakPending, 1);
}

// The default limit of one request is a run that never overlaps.
void TestDocumentTranslator::runsSerialAtOneRequest()
{
    m_api.answerDelayMs = 20;
    QCOMPARE(ConfigManager::instance()->intValue(Keys::apiMaxConcurrency), 1);
    QVERIFY(ConfigManager::instance()->boolValue(Keys::documentConcurrent));

    const QVector<DocumentWindow> windows = document(3, 3);
    TranslationContext context;
    DocumentTranslator translator;
    QString finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);

    QCOMPARE(m_api.requests, 3);
    QCOMPARE(m_api.peakPending, 1);
}

// Answers that arrive out of order still rebuild the document in order.
void TestDocumentTranslator::reassemblesDocumentsFromOverlappingAnswers()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 4);

    const QVector<DocumentWindow> windows = document(5, 2);
    TranslationContext context;
    DocumentTranslator translator;
    QString finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);

    // Every line is translated once, in document order, whatever order the
    // answers came back in.
    const QStringList source = DocumentSegmenter::assemble(windows, {}).split(QLatin1Char('\n'));
    const QStringList result = finished.split(QLatin1Char('\n'));
    QCOMPARE(result.size(), source.size());
    for (int line = 0; line < source.size(); ++line) {
        if (source.at(line).isEmpty())
            QCOMPARE(result.at(line), QString());
        else
            QCOMPARE(result.at(line), QStringLiteral("译文 %1").arg(source.at(line)));
    }
}

// The document reported while the run is under way carries each answer at its own
// place, whatever order the answers arrive in.
void TestDocumentTranslator::showsAnswersWhereTheyBelongWhateverTheirOrder()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 3);
    // The last window answers first, the middle one next, the first one last.
    m_api.answerDelays = {{QStringLiteral("\"1\": \"window 2 line 0\""), 20},
                          {QStringLiteral("\"1\": \"window 1 line 0\""), 120},
                          {QStringLiteral("\"1\": \"window 0 line 0\""), 240}};

    const QVector<DocumentWindow> windows = document(3, 3);
    TranslationContext context;
    DocumentTranslator translator;
    // Document reported at each accepted window, in the order they were reported.
    QStringList reports;
    QString finished;
    connect(&translator, &DocumentTranslator::windowTranslated, this,
            [&reports](const QString& text) { reports.append(text); });
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 15000);

    QCOMPARE(reports.size(), 3);

    // The first report holds the answer of the last window and the source text of
    // the windows the run has not accepted, each at its own place.
    const QStringList first = reports.at(0).split(QLatin1Char('\n'));
    QCOMPARE(first.at(0), QStringLiteral("window 0 line 0"));
    QCOMPARE(first.at(8), QStringLiteral("译文 window 2 line 0"));
    QVERIFY(reports.at(0).contains(QStringLiteral("window 1 line 0")));

    // The last report is the whole document translated.
    const QStringList last = reports.constLast().split(QLatin1Char('\n'));
    QCOMPARE(last.at(0), QStringLiteral("译文 window 0 line 0"));
    QCOMPARE(last.at(8), QStringLiteral("译文 window 2 line 0"));
    QCOMPARE(reports.constLast(), finished);
}

// One window failing does not disturb the windows answered beside it.
void TestDocumentTranslator::retriesOneWindowWhileOthersAreInFlight()
{
    m_api.answerDelayMs = 20;
    // The last window fails once and then answers after the others, so its retry
    // is certainly in flight while the earlier windows are accepted.
    m_api.failingPrompt = QStringLiteral("\"1\": \"window 3 line 0\"");
    m_api.answerDelays.append({m_api.failingPrompt, 300});
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 4);

    const QVector<DocumentWindow> windows = document(4, 3);
    TranslationContext context;
    DocumentTranslator translator;
    QString finished;
    int failures = 0;
    // Requests sent when the third window was accepted, and the highest request
    // count seen before the run finished.
    int requestsAtThird = -1;
    int completed = 0;
    connect(&translator, &DocumentTranslator::failed, this,
            [&failures](const ApiClient::Error&) { ++failures; });
    connect(&translator, &DocumentTranslator::progressChanged, this,
            [this, &requestsAtThird, &completed](int count, int) {
                completed = count;
                if (count == 3)
                    requestsAtThird = m_api.requests;
            });
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 20000);

    // The failed window is retried after the three windows beside it have been
    // accepted: the fifth request is the retry, and it went out past the third
    // completion rather than in the first batch.
    QCOMPARE(failures, 0);
    QCOMPARE(requestsAtThird, 4);
    QCOMPARE(m_api.requests, 5);
    QCOMPARE(completed, 4);
    QVERIFY(finished.startsWith(QStringLiteral("译文 window 0 line 0")));
    // The window that failed once is in the document like the others.
    QVERIFY(finished.contains(QStringLiteral("译文 window 3 line 2")));
}

// Stopping ends every request the run had in flight.
void TestDocumentTranslator::stopsEveryRequestInFlight()
{
    // Answers take long enough that the whole first batch is still on the wire
    // when the stop lands.
    m_api.answerDelayMs = 500;
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 4);

    const QVector<DocumentWindow> windows = document(6, 3);
    TranslationContext context;
    DocumentTranslator translator;
    bool stopped = false;
    QString finished;
    connect(&translator, &DocumentTranslator::stopped, this, [&stopped]() { stopped = true; });
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, context);
    // The first batch of four windows is out, and the fifth is still queued.
    QTRY_COMPARE_WITH_TIMEOUT(m_api.requests, 4, 10000);
    translator.stop();
    QTRY_VERIFY_WITH_TIMEOUT(stopped, 10000);

    // No further window is sent, and the answers still on the wire are not
    // reported as a finished document.
    QTest::qWait(700);
    QCOMPARE(m_api.requests, 4);
    QVERIFY(finished.isEmpty());
}

// One window giving up ends the run and the requests beside it.
void TestDocumentTranslator::dropsInFlightRequestsWhenAWindowGivesUp()
{
    m_api.answerDelayMs = 300;
    // The first window fails every time and answers ahead of the others, so the
    // requests beside it are still on the wire when it runs out of attempts.
    m_api.failingPrompt = QStringLiteral("\"1\": \"window 0 line 0\"");
    m_api.answerDelays.append({m_api.failingPrompt, 10});
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 3);
    ConfigManager::instance()->setValue(Keys::documentRetryCount, 0);

    const QVector<DocumentWindow> windows = document(5, 3);
    TranslationContext context;
    DocumentTranslator translator;
    ApiClient::Error failure;
    bool reported = false;
    bool finished = false;
    connect(&translator, &DocumentTranslator::failed, this,
            [&failure, &reported](const ApiClient::Error& error) {
                failure = error;
                reported = true;
            });
    connect(&translator, &DocumentTranslator::finished, this, [&finished](const QString&) {
        finished = true;
    });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(reported, 10000);
    QCOMPARE(failure.code, ApiClient::ErrorCode::ServerMessage);

    // Only the first batch went out, the answers still on the wire are dropped and
    // the queued windows never follow; the run reports one failure, not a
    // finished document.
    QTest::qWait(500);
    QCOMPARE(m_api.requests, 3);
    QCOMPARE(m_api.pending, 0);
    QVERIFY(!finished);
}

// A second run reuses the requests the first one left behind.
void TestDocumentTranslator::reusesTheSamePoolForASecondRun()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 2);

    TranslationContext context;
    DocumentTranslator translator;
    QString finished;
    int finishedRuns = 0;
    connect(&translator, &DocumentTranslator::finished, this, [&finished, &finishedRuns](const QString& text) {
        finished = text;
        ++finishedRuns;
    });

    const QVector<DocumentWindow> first = document(2, 3);
    translator.start(first, context);
    QTRY_COMPARE_WITH_TIMEOUT(finishedRuns, 1, 10000);
    QCOMPARE(finished.split(QLatin1Char('\n')).size(),
             DocumentSegmenter::assemble(first, {}).split(QLatin1Char('\n')).size());

    m_api.requests = 0;
    const QVector<DocumentWindow> second = document(4, 3);
    translator.start(second, context);
    QTRY_COMPARE_WITH_TIMEOUT(finishedRuns, 2, 10000);

    // The larger second run still sends every window once.
    QCOMPARE(m_api.requests, 4);
    const QStringList source = DocumentSegmenter::assemble(second, {}).split(QLatin1Char('\n'));
    const QStringList result = finished.split(QLatin1Char('\n'));
    QCOMPARE(result.size(), source.size());
    for (int line = 0; line < source.size(); ++line) {
        if (!source.at(line).isEmpty())
            QCOMPARE(result.at(line), QStringLiteral("译文 %1").arg(source.at(line)));
    }
}

void TestDocumentTranslator::resendsTheSamePromptOnRetry()
{
    m_api.failuresLeft = 1;

    const QVector<DocumentWindow> windows = document(2, 3);
    TranslationContext context;
    DocumentTranslator translator;
    QString finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    // One window per request plus the one the failure made necessary.
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
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, context);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);

    // An answer missing a key is sent again, and the retry repeats the request.
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

    // The document reported so far holds the accepted window translated and the
    // windows the run did not reach as their source text.
    QVERIFY(partial.startsWith(QStringLiteral("译文 window 0 line 0")));
    QVERIFY(partial.contains(QStringLiteral("window 1 line 0")));
    QVERIFY(partial.contains(QStringLiteral("window 2 line 0")));
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
    QString finished;
    connect(&translator, &DocumentTranslator::stopped, this, [&stopped]() { stopped = true; });
    connect(&translator, &DocumentTranslator::finished, this, [&finished](const QString& text) {
        finished = text;
    });

    // The first attempt fails, which leaves the run waiting out the retry pause.
    translator.start(windows, context);
    QTRY_COMPARE_WITH_TIMEOUT(m_api.requests, 1, 10000);
    translator.stop();
    QTRY_VERIFY_WITH_TIMEOUT(stopped, 10000);

    // A stop that lands while the retry waits must keep the request from going
    // out once the pause is over.
    QTest::qWait(1500);
    QCOMPARE(m_api.requests, 1);
    QVERIFY(finished.isEmpty());
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
