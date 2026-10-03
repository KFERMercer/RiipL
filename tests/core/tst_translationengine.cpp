#include <QtTest>

#include "TestSupport.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/translation/TranslationEngine.h"
#include "utils/TextUtils.h"

#include <QHostAddress>
#include <QTcpServer>

class TestTranslationEngine : public QObject
{
    Q_OBJECT

private slots:
    void parsesCandidateReplies();
    void resolvesReplacementSpans();
    void replacesCompleteWord();
    void absorbsRestatedNeighbour();
    void handlesRequestBodyParameters();
    void stopCancelsActiveRequest();
    void retriesEmptyCandidateReply();
    void sendsSystemPromptWithCandidates();
    void picksShortTextTemplate();
    void failedDispatchReturnsToIdle();
    void stopWhenIdleIsNoOp();
};

void TestTranslationEngine::parsesCandidateReplies()
{
    const QString json = QStringLiteral(
        "[{\"old\": \"皇帝\", \"new\": [\"君主\", \"帝王\"]}, "
        "{\"old\": \"莫卧儿\", \"new\": [\"蒙兀儿\"]}]");
    const QVector<TranslationEngine::CandidateGroup> groups =
        TranslationEngine::parseCandidateResponse(json);
    QCOMPARE(groups.size(), 2);
    QCOMPARE(groups.at(0).target, QStringLiteral("皇帝"));
    QCOMPARE(TestSupport::optionTexts(groups.at(0)), QStringList({QStringLiteral("君主"), QStringLiteral("帝王")}));
    QCOMPARE(groups.at(1).target, QStringLiteral("莫卧儿"));

    const QString fenced = QStringLiteral(
        "\n```json\n[{\"old\": \"皇帝\", \"new\": [\"君主\"]}]\n```\n");
    QCOMPARE(TranslationEngine::parseCandidateResponse(fenced).size(), 1);

    // A bare string in `new` is accepted, a group repeating its own target is
    // dropped, and a duplicated option is collapsed.
    const QString loose = QStringLiteral(
        "[{\"old\": \"皇帝\", \"new\": \"君主\"}, "
        "{\"old\": \"莫卧儿\", \"new\": [\"莫卧儿\", \"蒙兀儿\", \"蒙兀儿\"]}, "
        "{\"old\": \"\", \"new\": [\"空\"]}]");
    const QVector<TranslationEngine::CandidateGroup> parsed =
        TranslationEngine::parseCandidateResponse(loose);
    QCOMPARE(parsed.size(), 2);
    QCOMPARE(parsed.at(0).target, QStringLiteral("皇帝"));
    QCOMPARE(TestSupport::optionTexts(parsed.at(0)), QStringList({QStringLiteral("君主")}));
    QCOMPARE(TestSupport::optionTexts(parsed.at(1)), QStringList({QStringLiteral("蒙兀儿")}));

    QVERIFY(TranslationEngine::parseCandidateResponse(QStringLiteral("没有 JSON")).isEmpty());
}

void TestTranslationEngine::resolvesReplacementSpans()
{
    const QString text = QStringLiteral("The old man told the old story again.");
    const int manAt = text.indexOf(QStringLiteral("The old man"));
    const int storyAt = text.indexOf(QStringLiteral("the old story"));
    QVERIFY(manAt >= 0 && storyAt > manAt);

    // Two occurrences of the same word, each covered by a different target,
    // resolve to their own span.
    const TextUtils::WordSpan man = TextUtils::replacementSpan(
        text, manAt + 4, manAt + 7, QStringLiteral("the old man"), {});
    QCOMPARE(man.start, manAt);
    QCOMPARE(man.end, manAt + 11);
    const TextUtils::WordSpan story = TextUtils::replacementSpan(
        text, storyAt + 4, storyAt + 7, QStringLiteral("the old story"), {});
    QCOMPARE(story.start, storyAt);
    QCOMPARE(story.end, storyAt + 13);

    // A target that does not cover the selection is rejected even though it
    // appears in the text, which keeps the replacement off an unrelated word.
    QVERIFY(!TextUtils::replacementSpan(text, manAt + 4, manAt + 7,
                                        QStringLiteral("the old story"), {}).isValid());

    // A dropped sentence-initial capital still resolves, case-insensitively.
    const QString sentence = QStringLiteral("Wandering thoughts filled her mind.");
    const TextUtils::WordSpan folded = TextUtils::replacementSpan(
        sentence, 0, 9, QStringLiteral("wandering thoughts"), {});
    QCOMPARE(folded.start, 0);
    QCOMPARE(folded.end, 18);

    // The selection pins the occurrence: a repeated word resolves to the one the
    // user actually clicked rather than the first match in the text.
    const int secondCovered = storyAt + 4;
    const TextUtils::WordSpan bare = TextUtils::replacementSpan(
        text, secondCovered, secondCovered + 3, QStringLiteral("old"), {});
    QCOMPARE(bare.start, secondCovered);

    // Two overlapping occurrences that both cover the selection are genuinely
    // ambiguous and are rejected rather than guessed.
    QVERIFY(!TextUtils::replacementSpan(QStringLiteral("aaa"), 1, 2,
                                        QStringLiteral("aa"), {}).isValid());

    QVERIFY(!TextUtils::replacementSpan(text, manAt + 4, manAt + 7, QString(), {}).isValid());

    // A replacement that restates the character left outside the target absorbs
    // it, so a model that returned only the clicked character cannot splice its
    // alternative into the middle of the word.
    const QString chinese = QStringLiteral("傍晚时，小猫带着一桶鱼开心地回家了。");
    const int cat = chinese.indexOf(QStringLiteral("小猫"));
    const TextUtils::WordSpan grown = TextUtils::replacementSpan(
        chinese, cat + 1, cat + 2, QStringLiteral("猫"),
        QStringList{QStringLiteral("小猫咪")});
    QCOMPARE(grown.start, cat);
    QCOMPARE(grown.end, cat + 2);
    QCOMPARE(chinese.mid(grown.start, grown.length()), QStringLiteral("小猫"));

    // A replacement that shares no character with the neighbour leaves the span
    // alone, so an ordinary alternative is not widened by accident.
    const TextUtils::WordSpan plain = TextUtils::replacementSpan(
        chinese, cat + 1, cat + 2, QStringLiteral("猫"),
        QStringList{QStringLiteral("猫咪")});
    QCOMPARE(plain.start, cat + 1);
    QCOMPARE(plain.end, cat + 2);

    // Only the longest restated run per side is absorbed, and whitespace is
    // never crossed.
    const TextUtils::WordSpan spaced = TextUtils::replacementSpan(
        QStringLiteral("a cat"), 2, 5, QStringLiteral("cat"),
        QStringList{QStringLiteral("a cat")});
    QCOMPARE(spaced.start, 2);
    QCOMPARE(spaced.end, 5);
}

void TestTranslationEngine::replacesCompleteWord()
{
    const QString translated = QStringLiteral("莫卧儿皇帝是从什么时候开始觉得自己是印度人的？");
    const int huangIndex = translated.indexOf(QStringLiteral("皇"));
    const TextUtils::WordSpan span = TextUtils::wordSpanAt(translated, huangIndex);
    QVERIFY(span.isValid());

    // The clicked span acts only as an anchor; the target from the candidate
    // response may cover a longer run than the clicked word, and it resolves
    // back to the absolute span each replacement is applied to.
    const QString target = QStringLiteral("皇帝");
    const TextUtils::WordSpan resolved = TextUtils::replacementSpan(
        translated, span.start, span.end, target, {QStringLiteral("君主")});
    QVERIFY(resolved.isValid());
    QCOMPARE(resolved.start, span.start);
    QCOMPARE(translated.mid(resolved.start, resolved.length()), target);

    QString replaced = translated;
    replaced.replace(resolved.start, resolved.length(), QStringLiteral("君主"));
    QCOMPARE(replaced,
             QStringLiteral("莫卧儿君主是从什么时候开始觉得自己是印度人的？"));
}

void TestTranslationEngine::absorbsRestatedNeighbour()
{
    const QString text = QStringLiteral("傍晚时，小猫带着一桶鱼开心地回家了。");
    const int cat = text.indexOf(QStringLiteral("小猫"));
    QVERIFY(cat >= 0);

    // The click lands on the second character of the word and the model narrows
    // its target to that character alone, while the alternative restates the
    // first one. Taking `old` literally would splice `小猫咪` after the `小` left
    // behind and produce `小小猫咪`, so the span has to cover `小猫`.
    const QVector<TranslationEngine::CandidateGroup> parsed =
        TranslationEngine::parseCandidateResponse(
            QStringLiteral(R"([{"old":"猫","new":["小猫咪","猫咪"]}])"));
    const QVector<TranslationEngine::CandidateGroup> resolved =
        TranslationEngine::resolveGroups(parsed, text, cat + 1, cat + 2);
    QCOMPARE(resolved.size(), 1);
    QCOMPARE(resolved.first().start, cat);
    QCOMPARE(resolved.first().length, 2);
    for (const TranslationEngine::CandidateOption& option : resolved.first().options) {
        QCOMPARE(option.start, cat);
        QCOMPARE(option.length, 2);
        const QString replaced = text.left(option.start) + option.text
            + text.mid(option.start + option.length);
        QVERIFY(!replaced.contains(QStringLiteral("小小")));
    }
    QCOMPARE(text.left(resolved.first().start) + resolved.first().options.first().text
                 + text.mid(resolved.first().start + resolved.first().length),
             QStringLiteral("傍晚时，小猫咪带着一桶鱼开心地回家了。"));

    // An option that shares nothing with the neighbour keeps the narrower span,
    // so the absorption is driven by the text and not applied unconditionally.
    const QVector<TranslationEngine::CandidateGroup> plain =
        TranslationEngine::resolveGroups(
            TranslationEngine::parseCandidateResponse(
                QStringLiteral(R"([{"old":"猫","new":["猫咪"]}])")),
            text, cat + 1, cat + 2);
    QCOMPARE(plain.size(), 1);
    QCOMPARE(plain.first().options.first().start, cat + 1);
    QCOMPARE(plain.first().options.first().length, 1);
}

void TestTranslationEngine::handlesRequestBodyParameters()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    QCOMPARE(Defaults::apiTemperature, 0.0);
    const QJsonObject body = TranslationEngine::buildRequestBody(
        {QString(), QStringLiteral("Hello")}, false);
    QCOMPARE(body.value(QStringLiteral("temperature")).toDouble(), 0.0);

    // A negative temperature is the opt-out that leaves sampling to the provider.
    ConfigManager::instance()->setValue(Keys::apiTemperature, ApiTemperature::providerDefaultSentinel);
    const QJsonObject untuned = TranslationEngine::buildRequestBody(
        {QString(), QStringLiteral("Hello")}, false);
    QVERIFY(!untuned.contains(QStringLiteral("temperature")));

    ConfigManager::instance()->setValue(Keys::apiTemperature, 0.7);
    const QJsonObject tuned = TranslationEngine::buildRequestBody(
        {QString(), QStringLiteral("Hello")}, false);
    QCOMPARE(tuned.value(QStringLiteral("temperature")).toDouble(), 0.7);
}

void TestTranslationEngine::stopCancelsActiveRequest()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    // A listening server that never answers keeps the request in flight.
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    ConfigManager::instance()->setValue(Keys::apiBaseUrl,
        QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));

    TranslationEngine engine;
    QSignalSpy stoppedSpy(&engine, &TranslationEngine::stopped);
    QSignalSpy errorSpy(&engine, &TranslationEngine::errorOccurred);
    QSignalSpy stateSpy(&engine, &TranslationEngine::stateChanged);

    TranslationContext context;
    context.sourceText = QStringLiteral("Hello");
    context.targetLang = QStringLiteral("zh");
    engine.translateText(context);
    QVERIFY(engine.isBusy());

    engine.stop();

    QCOMPARE(stoppedSpy.count(), 1);
    QCOMPARE(errorSpy.count(), 0);
    QVERIFY(!engine.isBusy());
    QVERIFY(!stateSpy.isEmpty());
    QCOMPARE(stateSpy.last().last().toBool(), false);
}

// A model that answers without a single usable group gets exactly one more
// chance, and a reply that does carry groups is never retried.

void TestTranslationEngine::retriesEmptyCandidateReply()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()));

    int requests = 0;
    // The first reply names a fragment that does not cover the selection, which
    // is the failure the retry exists for; the second one is usable.
    const QByteArray offTargetReply =
        R"({"choices":[{"message":{"content":"[{\"old\":\"印度人\",\"new\":[\"南亚人\"]}]"}}]})";
    const QByteArray groupsReply =
        R"({"choices":[{"message":{"content":"[{\"old\":\"皇帝\",\"new\":[\"君主\"]}]"}}]})";
    QObject::connect(&server, &QTcpServer::newConnection, this, [&]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
            socket->readAll();
            ++requests;
            const QByteArray body = requests == 1 ? offTargetReply : groupsReply;
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->flush();
        });
    });

    TranslationEngine engine;
    TranslationContext context;
    context.translatedText = QStringLiteral("莫卧儿皇帝是从什么时候开始觉得自己是印度人的？");
    context.targetLang = QStringLiteral("zh");

    QVector<TranslationEngine::CandidateGroup> received;
    bool failed = false;
    const QString word = QStringLiteral("皇帝");
    const int start = context.translatedText.indexOf(word);
    engine.requestCandidates(
        context, start, start + word.size(),
        [&](const QVector<TranslationEngine::CandidateGroup>& groups) { received = groups; },
        [&](const ApiClient::Error&) { failed = true; });

    QTRY_COMPARE_WITH_TIMEOUT(received.size(), 1, 5000);
    QVERIFY(!failed);
    // The unusable first reply was retried, and the second one was taken as final.
    QCOMPARE(requests, 2);
    QCOMPARE(received.first().target, QStringLiteral("皇帝"));
    QCOMPARE(TestSupport::optionTexts(received.first()), QStringList({QStringLiteral("君主")}));
    // The engine hands back the resolved span, so the caller never re-searches.
    QCOMPARE(received.first().start, start);
    QCOMPARE(received.first().length, word.size());
    QVERIFY(received.first().isValid());
}

// The candidate request must open with the configured system prompt.

void TestTranslationEngine::sendsSystemPromptWithCandidates()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()));
    ConfigManager::instance()->setValue(Keys::promptSystem,
                                        QStringLiteral("You are a careful editor."));

    QByteArray payload;
    QByteArray request;
    QObject::connect(&server, &QTcpServer::newConnection, this, [&]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
            request += socket->readAll();
            if (!payload.isEmpty())
                return;
            const int headerEnd = request.indexOf("\r\n\r\n");
            if (headerEnd == -1)
                return;
            int contentLength = 0;
            for (const QByteArray& line : request.left(headerEnd).split('\n')) {
                if (line.toLower().startsWith("content-length:"))
                    contentLength = line.mid(int(line.indexOf(':')) + 1).trimmed().toInt();
            }
            if (request.size() - headerEnd - 4 < contentLength)
                return;
            payload = request.mid(headerEnd + 4, contentLength);
            const QByteArray body =
                R"({"choices":[{"message":{"content":"[{\"old\":\"皇帝\",\"new\":[\"君主\"]}]"}}]})";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->flush();
        });
    });

    TranslationEngine engine;
    TranslationContext context;
    context.translatedText = QStringLiteral("莫卧儿皇帝是从什么时候开始觉得自己是印度人的？");
    context.targetLang = QStringLiteral("zh");

    QVector<TranslationEngine::CandidateGroup> received;
    const QString word = QStringLiteral("皇帝");
    const int start = context.translatedText.indexOf(word);
    engine.requestCandidates(
        context, start, start + word.size(),
        [&](const QVector<TranslationEngine::CandidateGroup>& groups) { received = groups; },
        [](const ApiClient::Error&) {});

    QTRY_COMPARE_WITH_TIMEOUT(received.size(), 1, 5000);
    const QJsonObject sent = QJsonDocument::fromJson(payload).object();
    const QJsonArray messages = sent.value(QStringLiteral("messages")).toArray();
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).toObject().value(QStringLiteral("role")).toString(),
             QStringLiteral("system"));
    QCOMPARE(messages.at(0).toObject().value(QStringLiteral("content")).toString(),
             QStringLiteral("You are a careful editor."));
    QCOMPARE(messages.at(1).toObject().value(QStringLiteral("role")).toString(),
             QStringLiteral("user"));
}

// A translation short enough to sit inside the context window is looked up with
// the template that carries the source text; a longer one uses the local one.

void TestTranslationEngine::picksShortTextTemplate()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()));

    // Every request is answered, and its user message is kept for the check.
    QStringList prompts;
    QObject::connect(&server, &QTcpServer::newConnection, this, [&]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
            QByteArray request;
            forever {
                const QByteArray chunk = socket->readAll();
                if (chunk.isEmpty())
                    break;
                request += chunk;
            }
            const int headerEnd = request.indexOf("\r\n\r\n");
            if (headerEnd == -1)
                return;
            int contentLength = 0;
            for (const QByteArray& line : request.left(headerEnd).split('\n')) {
                if (line.toLower().startsWith("content-length:"))
                    contentLength = line.mid(int(line.indexOf(':')) + 1).trimmed().toInt();
            }
            if (request.size() - headerEnd - 4 < contentLength)
                return;
            prompts << TestSupport::requestUserPrompt(request.mid(headerEnd + 4, contentLength));
            const QByteArray body =
                R"({"choices":[{"message":{"content":"[{\"old\":\"皇帝\",\"new\":[\"君主\"]}]"}}]})";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->flush();
        });
    });

    TranslationEngine engine;
    TranslationContext context;
    context.sourceText = QStringLiteral("When did the Mughal emperor start to see himself as Indian?");
    context.targetLang = QStringLiteral("zh");
    const QString word = QStringLiteral("皇帝");

    const QString shortText = QStringLiteral("莫卧儿皇帝是从什么时候开始觉得自己是印度人的？");
    QVector<TranslationEngine::CandidateGroup> received;
    const auto request = [&](const QString& translatedText, int start) {
        context.translatedText = translatedText;
        received.clear();
        engine.requestCandidates(
            context, start, start + word.size(),
            [&](const QVector<TranslationEngine::CandidateGroup>& groups) { received = groups; },
            [](const ApiClient::Error&) {});
        QTRY_COMPARE_WITH_TIMEOUT(received.size(), 1, 5000);
    };

    request(shortText, shortText.indexOf(word));
    // The short template renders the whole translation with the selection marked
    // inside it, plus the source text.
    QCOMPARE(prompts.size(), 1);
    QVERIFY(prompts.first().contains(
        QStringLiteral("莫卧儿%1%2%3").arg(CandidateMarks::selectionOpen, word,
                                          CandidateMarks::selectionClose)));
    QVERIFY(prompts.first().contains(context.sourceText));
    QVERIFY(prompts.first().contains(shortText.left(shortText.indexOf(word))));

    const QString longText =
        shortText + QStringLiteral("这段补充说明让译文超出上下文窗口，从而落到按片段取词的模板上。");
    request(longText, longText.indexOf(word));
    // The local template carries the marked window instead, so neither the
    // source text nor the translation at large reaches the request.
    QCOMPARE(prompts.size(), 2);
    QVERIFY(!prompts.last().contains(context.sourceText));
    QVERIFY(!prompts.last().contains(longText));
    QVERIFY(prompts.last().contains(
        QStringLiteral("莫卧儿%1%2%3").arg(CandidateMarks::selectionOpen, word,
                                          CandidateMarks::selectionClose)));
}

void TestTranslationEngine::failedDispatchReturnsToIdle()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    ConfigManager::instance()->setValue(Keys::apiBaseUrl, QString());

    TranslationEngine engine;
    QSignalSpy stoppedSpy(&engine, &TranslationEngine::stopped);
    QSignalSpy errorSpy(&engine, &TranslationEngine::errorOccurred);
    QSignalSpy stateSpy(&engine, &TranslationEngine::stateChanged);

    TranslationContext context;
    context.sourceText = QStringLiteral("Hello");
    context.targetLang = QStringLiteral("zh");
    engine.translateText(context);

    QCOMPARE(errorSpy.count(), 1);
    // The failure is named rather than rendered, so a handler can translate it
    // when it is shown.
    QCOMPARE(errorSpy.first().first().value<ApiClient::Error>().code,
             ApiClient::ErrorCode::BaseUrlMissing);
    QCOMPARE(stoppedSpy.count(), 0);
    QVERIFY(!engine.isBusy());
    QCOMPARE(stateSpy.last().last().toBool(), false);
}

void TestTranslationEngine::stopWhenIdleIsNoOp()
{
    TranslationEngine engine;
    QSignalSpy stoppedSpy(&engine, &TranslationEngine::stopped);
    QSignalSpy stateSpy(&engine, &TranslationEngine::stateChanged);

    engine.stop();

    QCOMPARE(stoppedSpy.count(), 0);
    QVERIFY(!engine.isBusy());
    QVERIFY(stateSpy.isEmpty());
}

QTEST_MAIN(TestTranslationEngine)
#include "tst_translationengine.moc"
