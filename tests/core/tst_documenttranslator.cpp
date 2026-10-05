#include <QtTest>

#include "TestSupport.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/document/DocumentCache.h"
#include "core/document/DocumentSegmenter.h"
#include "core/translation/DocumentTranslator.h"
#include "utils/TextUtils.h"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <utility>

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
    QVector<std::pair<QString, int>> answerDelays;
    // Requests answered with a window of the wrong shape before one is answered
    // normally.
    int malformedLeft = 0;
    // Requests answered with the failure of an endpoint out of context: every
    // request arriving while another is unanswered. \p refusals counts them, so a
    // run that keeps its requests apart is told from one that does not.
    bool refusesConcurrent = false;
    int refusals = 0;
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
            peakPending = (std::max)(peakPending, pending);
            // An endpoint out of context refuses a request that arrives while
            // another is unanswered.
            const bool crowded = refusesConcurrent && pending > 1;
            if (crowded)
                ++refusals;
            // The delayed failure targets one window; a failure counter is spent
            // by whichever request arrives first.
            const bool named = !failingPrompt.isEmpty() && prompt.contains(failingPrompt);
            const bool failing = !crowded
                && ((named && !failingPromptSpent)
                    || (failingPrompt.isEmpty() && failuresLeft > 0));
            if (named && !failingPromptSpent)
                failingPromptSpent = true;
            const QByteArray answer = (crowded || failing) ? failure() : translationOf(prompt);
            if (failing && !named && failuresLeft > 0)
                --failuresLeft;
            if (!crowded && !failing && malformedLeft > 0)
                --malformedLeft;
            // A named failure answers ahead of the requests beside it, which
            // keeps them on the wire when the run gives up. A marker delay lets a
            // test place one window's answer before or after the others.
            int delay = answerDelayMs;
            for (const std::pair<QString, int>& marker : answerDelays) {
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
    // Every line of a window holds four words, so a window holds \p
    // linesPerWindow of them.
    const int words = TextUtils::wordCount(QStringLiteral("window 0 line 0"));
    return DocumentSegmenter::partition(lines.join(QLatin1Char('\n')),
                                        words * linesPerWindow, linesPerWindow);
}

// Cache files one document holds, sorted so a case can rely on their order.
QStringList cacheFiles(const DocumentCache& cache, const QString& suffix)
{
    return QDir(cache.documentDir()).entryList({QStringLiteral("*") + suffix}, QDir::Files,
                                               QDir::Name);
}

} // namespace

class TestDocumentTranslator : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void translatesEveryWindowInOrder();
    void foldsRepeatsAcrossBlankLines();
    void restoresWhitespaceAroundEveryLine();
    void sendsNeighboursWithoutTheirWhitespace();
    void sendsQuotedLinesAsEscapedValues();
    void sendsWindowsUpToTheConfiguredConcurrency();
    void lowersConcurrencyWhenTheEndpointRunsOutOfContext();
    void capsConcurrencyAfterTheLastWindow();
    void runsSerialWhenConcurrencyIsOff();
    void runsSerialAtOneRequest();
    void reassemblesDocumentsFromOverlappingAnswers();
    void showsAnswersWhereTheyBelongWhateverTheirOrder();
    void retriesOneWindowWhileOthersAreInFlight();
    void stopsEveryRequestInFlight();
    void leavesOutAWindowThatCannotBeTranslated();
    void reusesTheSamePoolForASecondRun();
    void cachesAcceptedAnswers();
    void cachesAnswersInLineOrder();
    void reusesCachedAnswersOnTheNextRun();
    void requestsOnlyTheWindowsTheCacheMisses();
    void separatesTheCacheByRequest();
    void separatesTheCacheByRequestBody();
    void ignoresCachedAnswersOfTheWrongShape();
    void stopsOnTheProgressOfAPartlyCachedRun();
    void keepsUnusableAnswersAsFailures();
    void namesEveryFailedShard();
    void clearsPreviousFailures();
    void resendsTheSamePromptOnRetry();
    void retriesAnswerOfTheWrongShape();
    void failsOnceMalformedAnswersAreExhausted();
    void failsOnceAttemptsAreExhausted();
    void retriesAgainOnTheNextRun();
    void retriesAsOftenAsConfigured();
    void usesTheDefaultRetryCount();
    void stopsDuringResultDelivery();
    void stopsWhileARetryIsPending();
    void finishesEmptyInputWithoutRequest();
    void failsEveryWindowOnceWithoutAnEndpoint();

private:
    StubApi m_api;
};

void TestDocumentTranslator::init()
{
    m_api.failuresLeft = 0;
    m_api.malformedLeft = 0;
    m_api.refusesConcurrent = false;
    m_api.refusals = 0;
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
    QVector<std::pair<int, int>> progress;
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

// A line repeated across blank lines reaches the model once, whatever blanks
// stand between its occurrences.
void TestDocumentTranslator::foldsRepeatsAcrossBlankLines()
{
    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(
        QStringLiteral("aaa\nbbb\n\nbbb\n\n\nbbb\nccc\n"));
    QCOMPARE(windows.size(), 1);
    QCOMPARE(windows.first().lineCount(), 3);

    TranslationContext context;
    DocumentTranslator translator;
    QString finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, context);
    QTRY_VERIFY(!finished.isEmpty());
    QCOMPARE(m_api.requests, 1);
    QCOMPARE(m_api.windows.first().keys(),
             QStringList({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")}));
    QCOMPARE(m_api.windows.first().value(QStringLiteral("2")).toString(), QStringLiteral("bbb"));

    // The answer of the folded line is written back at every occurrence, and the
    // blank lines the document holds come back with it.
    QCOMPARE(finished, QStringLiteral("译文 aaa\n译文 bbb\n\n译文 bbb\n\n\n译文 bbb\n译文 ccc\n"));
}

// The whitespace around a line never reaches the model, and the answer is written
// back between exactly those runs.
void TestDocumentTranslator::restoresWhitespaceAroundEveryLine()
{
    const QString document = QStringLiteral("\tfirst line \t\n"
                                            "    \n"
                                            "  second line  \n"
                                            "\tsecond line\t\n");
    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document);
    QCOMPARE(windows.size(), 1);

    TranslationContext context;
    DocumentTranslator translator;
    QString finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, context);
    QTRY_VERIFY(!finished.isEmpty());
    QCOMPARE(m_api.requests, 1);

    // Both occurrences of the repeated line reach the model as one line.
    const QJsonObject window = m_api.windows.first();
    QCOMPARE(window.keys(), QStringList({QStringLiteral("1"), QStringLiteral("2")}));
    QCOMPARE(window.value(QStringLiteral("1")).toString(), QStringLiteral("first line"));
    QCOMPARE(window.value(QStringLiteral("2")).toString(), QStringLiteral("second line"));

    QCOMPARE(finished, QStringLiteral("\t译文 first line \t\n    \n  译文 second line  \n"
                                      "\t译文 second line\t\n"));
}

// A neighbour reaches a request as its text alone.
void TestDocumentTranslator::sendsNeighboursWithoutTheirWhitespace()
{
    // A tab cannot come from the template, which indents with spaces.
    const QVector<DocumentWindow> windows =
        DocumentSegmenter::partition(QStringLiteral("\tfirst line\n\tsecond line\n"), 10, 1);
    QCOMPARE(windows.size(), 2);

    TranslationContext context;
    DocumentTranslator translator;
    QString finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, context);
    QTRY_VERIFY(!finished.isEmpty());
    QCOMPARE(m_api.requests, 2);
    for (const QString& prompt : std::as_const(m_api.prompts))
        QVERIFY2(!prompt.contains(QLatin1Char('\t')), qPrintable(prompt));

    QCOMPARE(finished, QStringLiteral("\t译文 first line\n\t译文 second line\n"));
}

// Original text that would break the JSON of a request is escaped in the value it
// is sent as.
void TestDocumentTranslator::sendsQuotedLinesAsEscapedValues()
{
    const QStringList lines = {QStringLiteral("\"I'm glad you could come to the party,\" He said."),
                               QStringLiteral("Path: C:\\tmp\\a.txt")};
    const QVector<DocumentWindow> windows =
        DocumentSegmenter::partition(lines.join(QLatin1Char('\n')));
    QCOMPARE(windows.size(), 1);

    TranslationContext context;
    DocumentTranslator translator;
    QString finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, context);
    QTRY_VERIFY(!finished.isEmpty());
    QCOMPARE(m_api.requests, 1);

    const QString escapedQuote =
        QStringLiteral("\"1\": \"\\\"I'm glad you could come to the party,\\\" He said.\"");
    QVERIFY2(m_api.prompts.first().contains(escapedQuote), qPrintable(m_api.prompts.first()));
    QCOMPARE(m_api.windows.first().value(QStringLiteral("1")).toString(), lines.at(0));
    QCOMPARE(m_api.windows.first().value(QStringLiteral("2")).toString(), lines.at(1));

    // The answer reaches the document unescaped.
    QCOMPARE(finished, QStringLiteral("译文 %1\n译文 %2").arg(lines.at(0), lines.at(1)));
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

// A run that meets an endpoint out of context keeps fewer requests in flight, so
// the windows it refused are still translated.
void TestDocumentTranslator::lowersConcurrencyWhenTheEndpointRunsOutOfContext()
{
    m_api.answerDelayMs = 20;
    m_api.refusesConcurrent = true;
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 4);

    const QVector<DocumentWindow> windows = document(6, 3);
    DocumentTranslator translator;
    QString finished;
    QStringList failed;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });
    connect(&translator, &DocumentTranslator::failed, this,
            [&failed](const QStringList& shards) { failed = shards; });

    translator.start(windows, TranslationContext());
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty() || !failed.isEmpty(), 20000);

    // Four requests went out together, the endpoint refused the three beside the
    // first, and the run finished the document one request at a time.
    QCOMPARE(m_api.peakPending, 4);
    QCOMPARE(m_api.refusals, 3);
    QCOMPARE(m_api.requests, windows.size() + 3);
    QVERIFY(failed.isEmpty());
    QVERIFY(!finished.isEmpty());
    for (const DocumentWindow& window : windows) {
        for (const DocumentLine& line : window.lines)
            QVERIFY2(finished.contains(QStringLiteral("译文 %1").arg(line.text)),
                     qPrintable(line.text));
    }
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
            [&failures](const QStringList&) { ++failures; });
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

// A window that cannot be translated is left out, and the run carries on with the
// windows beside it.
void TestDocumentTranslator::leavesOutAWindowThatCannotBeTranslated()
{
    m_api.answerDelayMs = 20;
    // The first window fails every time and answers ahead of the others.
    m_api.failingPrompt = QStringLiteral("\"1\": \"window 0 line 0\"");
    m_api.answerDelays.append({m_api.failingPrompt, 10});
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 3);
    ConfigManager::instance()->setValue(Keys::documentRetryCount, 0);

    const QVector<DocumentWindow> windows = document(5, 3);
    DocumentTranslator translator;
    QStringList failed;
    QString document;
    QString finished;
    connect(&translator, &DocumentTranslator::failed, this,
            [&failed](const QStringList& shards) { failed = shards; });
    connect(&translator, &DocumentTranslator::windowTranslated, this,
            [&document](const QString& text) { document = text; });
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, TranslationContext());
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 10000);

    // Every window was sent, and the one that failed is named by its number.
    QTest::qWait(300);
    QCOMPARE(m_api.requests, 5);
    QCOMPARE(failed.first().section(QLatin1Char('.'), 0).toInt(), 0);
    QVERIFY(finished.isEmpty());

    // The document holds the windows beside it translated, the failed window as
    // its source text.
    QVERIFY(document.contains(QStringLiteral("译文 window 4 line 0")));
    QVERIFY(document.contains(QStringLiteral("window 0 line 0")));
    QVERIFY(!document.contains(QStringLiteral("译文 window 0 line 0")));
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

// A run keeps the answer of every window it accepts, keyed by the request that
// produced it.
void TestDocumentTranslator::cachesAcceptedAnswers()
{
    const QVector<DocumentWindow> windows = document(2, 3);
    const DocumentCache cache(QStringLiteral("abc123"),
                              TestSupport::tempDir() + QStringLiteral("/cache"));

    DocumentTranslator translator;
    QString finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, TranslationContext(), cache);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);
    QCOMPARE(m_api.requests, 2);

    // One file per window, holding its lines keyed by line number.
    const QStringList files = cacheFiles(cache, QStringLiteral(".cache.json"));
    QCOMPARE(files.size(), 2);
    QSet<QString> firstLines;
    for (const QString& file : files) {
        QFile cached(cache.documentDir() + QLatin1Char('/') + file);
        QVERIFY(cached.open(QIODevice::ReadOnly));
        const QJsonObject answer = QJsonDocument::fromJson(cached.readAll()).object();
        QCOMPARE(answer.size(), 3);
        for (auto it = answer.constBegin(); it != answer.constEnd(); ++it)
            QVERIFY(it.value().toString().startsWith(QStringLiteral("译文 window ")));
        firstLines.insert(answer.value(QStringLiteral("1")).toString());
    }
    QCOMPARE(firstLines.size(), 2);
}

// A cache file lists its lines in the order of the document: the text order of
// the keys would put line 10 before line 2.
void TestDocumentTranslator::cachesAnswersInLineOrder()
{
    // Ten lines, so the text order of the keys would show.
    const QVector<DocumentWindow> windows = document(1, 10);
    const int count = windows.first().lineCount();
    QVERIFY(count > 9);
    const DocumentCache cache(QStringLiteral("abc123"),
                              TestSupport::tempDir() + QStringLiteral("/cache"));

    DocumentTranslator translator;
    QString finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, TranslationContext(), cache);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);

    const QStringList files = cacheFiles(cache, QStringLiteral(".cache.json"));
    QCOMPARE(files.size(), 1);
    QFile cached(cache.documentDir() + QLatin1Char('/') + files.first());
    QVERIFY(cached.open(QIODevice::ReadOnly));
    const QString answer = QString::fromUtf8(cached.readAll());

    int previous = -1;
    QStringList lines;
    for (int line = 1; line <= count; ++line) {
        const int at = answer.indexOf(QStringLiteral("\"%1\": ").arg(line));
        QVERIFY2(at > previous, qPrintable(answer));
        previous = at;
    }
    for (const DocumentLine& line : windows.first().lines)
        lines << QStringLiteral("译文 %1").arg(line.text);
    QCOMPARE(answer, PromptBuilder::documentWindowData(lines));
}

// A document the cache holds in full is translated without a single request.
void TestDocumentTranslator::reusesCachedAnswersOnTheNextRun()
{
    const QVector<DocumentWindow> windows = document(3, 3);
    const DocumentCache cache(QStringLiteral("abc123"),
                              TestSupport::tempDir() + QStringLiteral("/cache"));

    DocumentTranslator translator;
    QStringList finished;
    QVector<std::pair<int, int>> progress;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished.append(text); });
    connect(&translator, &DocumentTranslator::progressChanged, this,
            [&progress](int completed, int total) { progress.append({completed, total}); });

    translator.start(windows, TranslationContext(), cache);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    QCOMPARE(m_api.requests, 3);

    m_api.requests = 0;
    progress.clear();
    translator.start(windows, TranslationContext(), cache);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 10000);

    // The run answers itself from the cache and reports the document whole.
    QCOMPARE(m_api.requests, 0);
    QCOMPARE(finished.at(1), finished.at(0));
    QCOMPARE(progress.size(), 1);
    QCOMPARE(progress.first(), qMakePair(3, 3));
}

// A partly cached document sends only the windows the cache does not hold.
void TestDocumentTranslator::requestsOnlyTheWindowsTheCacheMisses()
{
    const QVector<DocumentWindow> windows = document(3, 3);
    const DocumentCache cache(QStringLiteral("abc123"),
                              TestSupport::tempDir() + QStringLiteral("/cache"));

    DocumentTranslator translator;
    QStringList finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished.append(text); });

    translator.start(windows, TranslationContext(), cache);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);

    // One cached window is dropped, as a run that failed before would have left it.
    const QStringList files = cacheFiles(cache, QStringLiteral(".cache.json"));
    QCOMPARE(files.size(), 3);
    QVERIFY(QFile::remove(cache.documentDir() + QLatin1Char('/') + files.at(1)));

    m_api.requests = 0;
    translator.start(windows, TranslationContext(), cache);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 10000);

    QCOMPARE(m_api.requests, 1);
    QCOMPARE(finished.at(1), finished.at(0));
    QCOMPARE(cacheFiles(cache, QStringLiteral(".cache.json")).size(), 3);
}

// The answer follows the request, so another target language asks again instead
// of answering in the language of the previous run.
void TestDocumentTranslator::separatesTheCacheByRequest()
{
    const QVector<DocumentWindow> windows = document(1, 3);
    const DocumentCache cache(QStringLiteral("abc123"),
                              TestSupport::tempDir() + QStringLiteral("/cache"));

    DocumentTranslator translator;
    QStringList finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished.append(text); });

    TranslationContext context;
    context.targetLang = QStringLiteral("zh");
    translator.start(windows, context, cache);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);

    m_api.requests = 0;
    context.targetLang = QStringLiteral("en");
    translator.start(windows, context, cache);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 10000);

    QCOMPARE(m_api.requests, 1);
}

// The cache key covers the body a window is sent with, so another model or an
// extra body that overrides it asks again, while the same request is reused.
void TestDocumentTranslator::separatesTheCacheByRequestBody()
{
    const QVector<DocumentWindow> windows = document(1, 3);
    const DocumentCache cache(QStringLiteral("abc123"),
                              TestSupport::tempDir() + QStringLiteral("/cache"));

    DocumentTranslator translator;
    QStringList finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished.append(text); });

    translator.start(windows, TranslationContext(), cache);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);

    ConfigManager::instance()->setValue(Keys::apiModel, QStringLiteral("other-model"));
    m_api.requests = 0;
    translator.start(windows, TranslationContext(), cache);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 10000);
    QCOMPARE(m_api.requests, 1);

    ConfigManager::instance()->setValue(Keys::apiExtraBody,
                                        QStringLiteral("{\"model\": \"third-model\"}"));
    m_api.requests = 0;
    translator.start(windows, TranslationContext(), cache);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 3, 10000);
    QCOMPARE(m_api.requests, 1);

    m_api.requests = 0;
    translator.start(windows, TranslationContext(), cache);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 4, 10000);
    QCOMPARE(m_api.requests, 0);
}

// A stop that lands on the report of the cached windows keeps the run from
// reaching the model.
void TestDocumentTranslator::stopsOnTheProgressOfAPartlyCachedRun()
{
    const QVector<DocumentWindow> windows = document(3, 3);
    const DocumentCache cache(QStringLiteral("abc123"),
                              TestSupport::tempDir() + QStringLiteral("/cache"));

    DocumentTranslator translator;
    QStringList finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished.append(text); });

    translator.start(windows, TranslationContext(), cache);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);

    // Only the first window stays cached, so the run reports progress before it
    // sends anything.
    const QStringList files = cacheFiles(cache, QStringLiteral(".cache.json"));
    QCOMPARE(files.size(), 3);
    for (int index = 1; index < files.size(); ++index)
        QVERIFY(QFile::remove(cache.documentDir() + QLatin1Char('/') + files.at(index)));

    bool stopped = false;
    connect(&translator, &DocumentTranslator::progressChanged, this,
            [&translator](int completed, int) {
                if (completed == 1)
                    translator.stop();
            });
    connect(&translator, &DocumentTranslator::stopped, this, [&stopped]() { stopped = true; });

    m_api.requests = 0;
    translator.start(windows, TranslationContext(), cache);
    QTRY_VERIFY_WITH_TIMEOUT(stopped, 10000);

    QTest::qWait(200);
    QCOMPARE(m_api.requests, 0);
    QCOMPARE(finished.size(), 1);
}

// A stored answer that does not fit the window is translated again rather than
// rendered into the document.
void TestDocumentTranslator::ignoresCachedAnswersOfTheWrongShape()
{
    const QVector<DocumentWindow> windows = document(1, 3);
    const DocumentCache cache(QStringLiteral("abc123"),
                              TestSupport::tempDir() + QStringLiteral("/cache"));

    DocumentTranslator translator;
    QStringList finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished.append(text); });

    translator.start(windows, TranslationContext(), cache);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);

    const QStringList files = cacheFiles(cache, QStringLiteral(".cache.json"));
    QCOMPARE(files.size(), 1);
    const QString path = cache.documentDir() + QLatin1Char('/') + files.first();
    QFile stale(path);
    QVERIFY(stale.open(QIODevice::WriteOnly | QIODevice::Truncate));
    stale.write("{\"1\": \"译文\"}");
    stale.close();

    m_api.requests = 0;
    translator.start(windows, TranslationContext(), cache);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 10000);

    QCOMPARE(m_api.requests, 1);
    QVERIFY(stale.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(stale.readAll()).object().size(), 3);
}

// A reply the translator cannot read is kept as it came back, and the next run
// replaces it with the answer that succeeded.
void TestDocumentTranslator::keepsUnusableAnswersAsFailures()
{
    m_api.malformedLeft = 10;
    ConfigManager::instance()->setValue(Keys::documentRetryCount, 0);

    const QVector<DocumentWindow> windows = document(1, 3);
    const DocumentCache cache(QStringLiteral("abc123"),
                              TestSupport::tempDir() + QStringLiteral("/cache"));

    DocumentTranslator translator;
    bool reported = false;
    connect(&translator, &DocumentTranslator::failed, this,
            [&reported](const QStringList&) { reported = true; });

    translator.start(windows, TranslationContext(), cache);
    QTRY_VERIFY_WITH_TIMEOUT(reported, 10000);

    // The unusable answer is stored with the line it lost, and no entry is
    // written for a window the run did not accept.
    const QStringList failures = cacheFiles(cache, QStringLiteral(".failure.json"));
    QCOMPARE(failures.size(), 1);
    QFile broken(cache.documentDir() + QLatin1Char('/') + failures.first());
    QVERIFY(broken.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(broken.readAll()).object().size(), 2);
    QVERIFY(cacheFiles(cache, QStringLiteral(".cache.json")).isEmpty());

    // The next run reaches the model again, and its answer takes the place of
    // the failure.
    m_api.malformedLeft = 0;
    QString finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, TranslationContext(), cache);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);

    QCOMPARE(m_api.requests, 2);
    QVERIFY(cacheFiles(cache, QStringLiteral(".failure.json")).isEmpty());
    QCOMPARE(cacheFiles(cache, QStringLiteral(".cache.json")).size(), 1);
}

// Every window the run could not translate is named, whether or not the run
// caches.
void TestDocumentTranslator::namesEveryFailedShard()
{
    m_api.malformedLeft = 10;
    ConfigManager::instance()->setValue(Keys::documentRetryCount, 0);

    const QVector<DocumentWindow> windows = document(3, 3);
    const DocumentCache cache(QStringLiteral("abc123"),
                              TestSupport::tempDir() + QStringLiteral("/cache"));

    DocumentTranslator translator;
    QStringList failed;
    connect(&translator, &DocumentTranslator::failed, this,
            [&failed](const QStringList& shards) { failed = shards; });

    translator.start(windows, TranslationContext(), cache);
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 3, 10000);

    // The names are the ones the failure files carry.
    const QStringList files = cacheFiles(cache, QStringLiteral(".failure.json"));
    QCOMPARE(files.size(), 3);
    for (const QString& shard : failed)
        QVERIFY(files.contains(shard + QStringLiteral(".failure.json")));

    // A run without a cache names the same windows.
    const QStringList cached = failed;
    m_api.malformedLeft = 10;
    failed.clear();
    translator.start(windows, TranslationContext());
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 3, 10000);
    QCOMPARE(failed, cached);
}

// A run drops the failures the previous one left behind before it starts.
void TestDocumentTranslator::clearsPreviousFailures()
{
    const QVector<DocumentWindow> windows = document(1, 3);
    const DocumentCache cache(QStringLiteral("abc123"),
                              TestSupport::tempDir() + QStringLiteral("/cache"));
    QVERIFY(cache.storeFailure(QStringLiteral("deadbeef.0"), QStringLiteral("{\"1\":")));

    DocumentTranslator translator;
    QString finished;
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, TranslationContext(), cache);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);

    QVERIFY(cacheFiles(cache, QStringLiteral(".failure.json")).isEmpty());
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
    DocumentTranslator translator;
    QStringList failed;
    bool done = false;
    connect(&translator, &DocumentTranslator::failed, this,
            [&failed](const QStringList& shards) { failed = shards; });
    connect(&translator, &DocumentTranslator::finished, this, [&done](const QString&) { done = true; });

    translator.start(windows, TranslationContext());
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 2, 10000);

    // A wrong shape is retried like any other failed window, and the run carries on
    // to the window beside it, so both are reported once their attempts are used.
    QVERIFY(!done);
    QCOMPARE(m_api.requests, 6);
}

void TestDocumentTranslator::failsOnceAttemptsAreExhausted()
{
    m_api.failuresLeft = 10;

    const QVector<DocumentWindow> windows = document(2, 3);
    DocumentTranslator translator;
    QStringList failed;
    bool done = false;
    connect(&translator, &DocumentTranslator::failed, this,
            [&failed](const QStringList& shards) { failed = shards; });
    connect(&translator, &DocumentTranslator::finished, this, [&done](const QString&) { done = true; });

    translator.start(windows, TranslationContext());
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 2, 10000);
    QVERIFY(!done);
    QCOMPARE(m_api.requests, 6);
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
    connect(&translator, &DocumentTranslator::failed, this, [&failures](const QStringList&) {
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
            [&failures](const QStringList&) { ++failures; });

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
            [&failures](const QStringList&) { ++failures; });

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

// A window failing on its own stack must not be dispatched twice, which would
// settle it before the windows behind it were sent.
void TestDocumentTranslator::failsEveryWindowOnceWithoutAnEndpoint()
{
    ConfigManager* config = ConfigManager::instance();
    config->setValue(Keys::apiBaseUrl, QString());
    config->setValue(Keys::documentRetryCount, 0);

    const QVector<DocumentWindow> windows = document(4, 3);
    const int count = windows.size();

    DocumentTranslator translator;
    QStringList failed;
    QString finished;
    connect(&translator, &DocumentTranslator::failed, this,
            [&failed](const QStringList& shards) { failed = shards; });
    connect(&translator, &DocumentTranslator::finished, this,
            [&finished](const QString& text) { finished = text; });

    translator.start(windows, TranslationContext());
    QTRY_COMPARE(failed.size(), count);
    QVERIFY(finished.isEmpty());
    QCOMPARE(m_api.requests, 0);

    // One entry per window, in document order, so none was skipped or counted
    // twice.
    QCOMPARE(QSet<QString>(failed.cbegin(), failed.cend()).size(), count);
    QVERIFY(failed.first().startsWith(QStringLiteral("0.")));
    QVERIFY(failed.last().startsWith(QString::number(count - 1) + QLatin1Char('.')));
}

QTEST_MAIN(TestDocumentTranslator)
#include "tst_documenttranslator.moc"
