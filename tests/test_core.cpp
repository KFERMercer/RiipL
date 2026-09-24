#include <QtTest>

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/history/HistoryManager.h"
#include "core/models/Glossary.h"
#include "core/translation/Language.h"
#include "core/translation/PromptBuilder.h"
#include "core/translation/TranslationEngine.h"
#include "utils/SingleInstance.h"
#include "utils/TextUtils.h"

#include <QJsonDocument>
#include <QFileInfo>
#include <QHostAddress>
#include <QHttpHeaders>
#include <QTcpServer>
#include <QTemporaryDir>

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    void configFallsBackToDefaults();
    void configStoresOnlyNonDefaults();
    void configPersistsAcrossInstances();
    void configFlushesPendingSaveOnRecreate();
    void promptSubstitution();
    void promptGlossaryFormatting();
    void candidatePromptSubstitution();
    void knownPlaceholdersCoverVariables();
    void glossaryRoundTrip();
    void historyTrimming();
    void historyDebouncesSavesUntilFlush();
    void uiLanguageResolution();
    void candidateCleaning();
    void wordSpanAtBoundaries();
    void candidateResponseParsing();
    void replaceTargetsCompleteWord();
    void singleInstanceArbitration();
    void guessFromScriptDetection();
    void scriptDetectionCoversSupplementaryPlanes();
    void resolveAutoExcludesTarget();
    void requestBodyParameterHandling();
    void customHeaderLineParsing();
    void chatCompletionsUrlDerivation();
    void configValueEqualityMatchesDefaults();
    void stopCancelsActiveRequest();
    void failedDispatchReturnsToIdle();
    void stopWhenIdleIsNoOp();

private:
    QString tempDir()
    {
        static QTemporaryDir dir;
        return dir.path() + QStringLiteral("/%1").arg(QTest::currentTestFunction());
    }
};

void TestCore::configFallsBackToDefaults()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());
    QCOMPARE(ConfigManager::instance()->stringValue(Keys::apiModel), Defaults::apiModel);
    QCOMPARE(ConfigManager::instance()->doubleValue(Keys::apiTemperature), Defaults::apiTemperature);
    QCOMPARE(ConfigManager::instance()->intValue(Keys::apiTimeoutMs), Defaults::apiTimeoutMs);
    QCOMPARE(ConfigManager::instance()->stringValue(Keys::translationTargetLang), QStringLiteral("zh"));
    QVERIFY(ConfigManager::instance()->isDefault(Keys::apiModel));
    QFile file(ConfigManager::instance()->configFilePath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(file.readAll()).object(), QJsonObject());
}

void TestCore::configStoresOnlyNonDefaults()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());

    ConfigManager::instance()->setValue(Keys::apiTemperature, 0.7);
    QVERIFY(!ConfigManager::instance()->isDefault(Keys::apiTemperature));
    QCOMPARE(ConfigManager::instance()->doubleValue(Keys::apiTemperature), 0.7);

    ConfigManager::instance()->flush();
    {
        QFile file(ConfigManager::instance()->configFilePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject doc = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(doc.value(QStringLiteral("api")).toObject().value(QStringLiteral("temperature")).toDouble(), 0.7);
        QVERIFY(!doc.value(QStringLiteral("api")).toObject().contains(QStringLiteral("stream")));
    }

    ConfigManager::instance()->setValue(Keys::apiTemperature, Defaults::apiTemperature);
    ConfigManager::instance()->flush();
    {
        QFile file(ConfigManager::instance()->configFilePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject after = QJsonDocument::fromJson(file.readAll()).object();
        QVERIFY(!after.value(QStringLiteral("api")).toObject().contains(QStringLiteral("temperature")));
    }
    QVERIFY(ConfigManager::instance()->isDefault(Keys::apiTemperature));
}

void TestCore::configPersistsAcrossInstances()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());
    ConfigManager::instance()->setValue(Keys::uiLanguage, QStringLiteral("zh"));
    ConfigManager::instance()->setValue(Keys::translationTargetLang, QStringLiteral("ja"));
    ConfigManager::instance()->flush();

    ConfigManager::createInstance(tempDir());
    QCOMPARE(ConfigManager::instance()->stringValue(Keys::uiLanguage), QStringLiteral("zh"));
    QCOMPARE(ConfigManager::instance()->stringValue(Keys::translationTargetLang), QStringLiteral("ja"));
    QCOMPARE(ConfigManager::instance()->intValue(Keys::uiAutoTranslateDelay), 800);
}

void TestCore::configFlushesPendingSaveOnRecreate()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());
    ConfigManager::instance()->setValue(Keys::translationTargetLang, QStringLiteral("ja"));
    ConfigManager::createInstance(tempDir());
    QCOMPARE(ConfigManager::instance()->stringValue(Keys::translationTargetLang),
             QStringLiteral("ja"));
}

void TestCore::promptSubstitution()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());

    TranslationContext context;
    context.sourceText = QStringLiteral("Hello");
    context.targetLang = QStringLiteral("zh");
    context.uiLanguage = QStringLiteral("zh");
    QCOMPARE(PromptBuilder::build(context).system, Defaults::promptSystemZh);

    context.uiLanguage = QStringLiteral("en");
    PromptBuilder::Result result = PromptBuilder::build(context);
    QCOMPARE(result.system, Defaults::promptSystemEn);
    QVERIFY(result.user.contains(QStringLiteral("Chinese")));
    QVERIFY(result.user.contains(QStringLiteral("Hello")));
    QVERIFY(!result.user.contains(QStringLiteral("{target_lang}")));
    QVERIFY(!result.user.contains(QStringLiteral("{source_text}")));

    // A configured template overrides the built-in default system prompt.
    ConfigManager::instance()->setValue(Keys::promptSystemEn, QStringLiteral("You are an expert translator."));
    result = PromptBuilder::build(context);
    QCOMPARE(result.system, QStringLiteral("You are an expert translator."));
}

void TestCore::promptGlossaryFormatting()
{
    QVector<GlossaryEntry> entries;
    entries.append({QStringLiteral("苹果"), QStringLiteral("Apple")});
    entries.append({QStringLiteral("RiipL"), QString()});

    const QStringList lines = PromptBuilder::glossaryLines(entries);
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(0), QStringLiteral("苹果 translates to Apple"));
    QCOMPARE(lines.at(1), QStringLiteral("RiipL (leave untranslated)"));

    TranslationContext context;
    context.sourceText = QStringLiteral("苹果 is good");
    context.targetLang = QStringLiteral("en");
    context.glossary = entries;
    context.uiLanguage = QStringLiteral("zh");
    const PromptBuilder::Result result = PromptBuilder::build(context);
    QVERIFY(result.user.contains(QStringLiteral("translates to")));
    QVERIFY(result.user.contains(QStringLiteral("(leave untranslated)")));
}


void TestCore::candidatePromptSubstitution()
{
    const QString prompt = PromptBuilder::candidatePrompt(
        QStringLiteral("Hello world"), QStringLiteral("你好，世界"), QStringLiteral("世界"),
        QStringLiteral("zh"), QStringLiteral("en"));
    QVERIFY(prompt.contains(QStringLiteral("Hello world")));
    QVERIFY(prompt.contains(QStringLiteral("你好，世界")));
    QVERIFY(prompt.contains(QStringLiteral("世界")));
    QVERIFY(prompt.contains(QStringLiteral("alternative")));
    QVERIFY(prompt.contains(QStringLiteral("Chinese")));
    QVERIFY(!prompt.contains(QStringLiteral("{target_lang}")));
}

void TestCore::knownPlaceholdersCoverVariables()
{
    const QStringList placeholders = PromptBuilder::knownPlaceholders();
    QCOMPARE(placeholders.size(), QSet<QString>(placeholders.cbegin(), placeholders.cend()).size());

    for (const QString& name : placeholders)
        QVERIFY(PromptBuilder::substitute(QStringLiteral("{%1}").arg(name),
                                          {{name, QStringLiteral("x")}}) == QStringLiteral("x"));
}

void TestCore::glossaryRoundTrip()
{
    QVector<GlossaryEntry> entries;
    entries.append({QStringLiteral("术语"), QStringLiteral("term")});
    entries.append({QStringLiteral("专有名词"), QString()});
    const QJsonArray array = Glossary::toJson(entries);
    const QVector<GlossaryEntry> restored = Glossary::fromJson(array);
    QCOMPARE(restored.size(), 2);
    QVERIFY(restored.first() == entries.first());
    QVERIFY(restored.last() == entries.last());
}

void TestCore::historyTrimming()
{
    QDir().mkpath(tempDir());
    const QString path = tempDir() + QStringLiteral("/history.json");

    {
        HistoryManager manager(path);
        manager.setMaxRecords(3);
        for (int i = 0; i < 5; ++i) {
            TranslationRecord record;
            record.timestamp = i + 1;
            record.source = QStringLiteral("s%1").arg(i);
            record.target = QStringLiteral("t%1").arg(i);
            record.sourceLang = QStringLiteral("auto");
            record.targetLang = QStringLiteral("en");
            manager.addRecord(record);
        }
        QCOMPARE(manager.records().size(), 3);
        QCOMPARE(manager.records().first().source, QStringLiteral("s4"));
        QCOMPARE(manager.records().last().source, QStringLiteral("s2"));
    }

    HistoryManager reloaded(path);
    reloaded.setMaxRecords(500);
    QCOMPARE(reloaded.records().size(), 3);
}

void TestCore::historyDebouncesSavesUntilFlush()
{
    QDir().mkpath(tempDir());
    const QString path = tempDir() + QStringLiteral("/history.json");

    HistoryManager manager(path);
    TranslationRecord record;
    record.timestamp = 1;
    record.source = QStringLiteral("hello");
    record.target = QStringLiteral("你好");
    record.sourceLang = QStringLiteral("auto");
    record.targetLang = QStringLiteral("zh");
    manager.addRecord(record);

    QVERIFY(!QFileInfo::exists(path));

    manager.flush();

    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
        QCOMPARE(array.size(), 1);
        QCOMPARE(array.first().toObject().value(QStringLiteral("source")).toString(),
                 QStringLiteral("hello"));
    }
}

void TestCore::uiLanguageResolution()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());
    QVERIFY(ConfigManager::instance()->isDefault(Keys::uiLanguage));

    const QString resolved = ConfigManager::instance()->resolvedUiLanguage();
    QVERIFY(resolved == QLatin1String("en") || resolved == QLatin1String("zh"));

    ConfigManager::instance()->setValue(Keys::uiLanguage, QStringLiteral("zh"));
    QCOMPARE(ConfigManager::instance()->resolvedUiLanguage(), QStringLiteral("zh"));
    ConfigManager::instance()->setValue(Keys::uiLanguage, QStringLiteral("en"));
    QCOMPARE(ConfigManager::instance()->resolvedUiLanguage(), QStringLiteral("en"));
}

void TestCore::candidateCleaning()
{
    const QStringList raw = {
        QStringLiteral("1. alternative one"),
        QStringLiteral("- alternative two"),
        QStringLiteral("* alternative three"),
        QStringLiteral("`alternative four`"),
        QStringLiteral("\"alternative five\""),
        QStringLiteral("   "),
        QStringLiteral("alternative six")
    };
    const QStringList cleaned = TranslationEngine::cleanCandidates(raw);
    QCOMPARE(cleaned.size(), 6);
    QCOMPARE(cleaned.at(0), QStringLiteral("alternative one"));
    QCOMPARE(cleaned.at(1), QStringLiteral("alternative two"));
    QCOMPARE(cleaned.at(2), QStringLiteral("alternative three"));
    QCOMPARE(cleaned.at(3), QStringLiteral("alternative four"));
    QCOMPARE(cleaned.at(4), QStringLiteral("alternative five"));
    QCOMPARE(cleaned.at(5), QStringLiteral("alternative six"));
}

void TestCore::wordSpanAtBoundaries()
{
    const QString english = QStringLiteral("Hello world");
    const TextUtils::WordSpan hello = TextUtils::wordSpanAt(english, 1);
    QVERIFY(hello.valid());
    QCOMPARE(english.mid(hello.start, hello.length()), QStringLiteral("Hello"));

    const TextUtils::WordSpan world = TextUtils::wordSpanAt(english, 8);
    QVERIFY(world.valid());
    QCOMPARE(english.mid(world.start, world.length()), QStringLiteral("world"));

    QVERIFY(!TextUtils::wordSpanAt(english, 5).valid());
    QVERIFY(TextUtils::wordSpanAt(english, -5).valid());

    const QString cjk = QStringLiteral("\u4f60\u597d\uff0c\u4e16\u754c\uff01");
    const int shiIndex = cjk.indexOf(QStringLiteral("\u4e16"));
    const TextUtils::WordSpan span = TextUtils::wordSpanAt(cjk, shiIndex);
    QVERIFY(span.valid());
    const QString segment = cjk.mid(span.start, span.length());
    QVERIFY(!segment.contains(QChar(0xFF0C)));
    QVERIFY(segment.size() <= 8);

    const QString longRun = QStringLiteral("\u8fd9\u662f\u4e00\u6bb5\u6ca1\u6709\u4efb\u4f55\u6807\u70b9\u7684\u5f88\u957f\u4e2d\u6587\u6587\u672c");
    const TextUtils::WordSpan longSpan = TextUtils::wordSpanAt(longRun, longRun.size() / 2);
    if (longSpan.valid())
        QVERIFY(longSpan.length() <= 8);
}

void TestCore::candidateResponseParsing()
{
    const QString json = QStringLiteral(
        "{\"replace\": \"皇帝\", \"options\": [\"君主\", \"统治者\", \"帝王\"]}");
    const TranslationEngine::CandidateResult parsed = TranslationEngine::parseCandidateResponse(json);
    QCOMPARE(parsed.replaceTarget, QStringLiteral("皇帝"));
    QCOMPARE(parsed.options.size(), 3);
    QCOMPARE(parsed.options.at(0), QStringLiteral("君主"));

    const QString fenced = QStringLiteral("\n```json\n{\"replace\": \"皇帝\", \"options\": [\"君主\"]}\n```\n");
    const TranslationEngine::CandidateResult fromFence = TranslationEngine::parseCandidateResponse(fenced);
    QCOMPARE(fromFence.replaceTarget, QStringLiteral("皇帝"));
    QCOMPARE(fromFence.options, QStringList{QStringLiteral("君主")});

    const QString plain = QStringLiteral("君主\n统治者\n\"帝王\"");
    const TranslationEngine::CandidateResult fallback = TranslationEngine::parseCandidateResponse(plain);
    QVERIFY(fallback.replaceTarget.isEmpty());
    QCOMPARE(fallback.options.size(), 3);
    QCOMPARE(fallback.options.at(2), QStringLiteral("帝王"));
}

void TestCore::replaceTargetsCompleteWord()
{
    const QString translated = QStringLiteral("莫卧儿皇帝是从什么时候开始觉得自己是印度人的？");
    const int huangIndex = translated.indexOf(QStringLiteral("皇"));
    const TextUtils::WordSpan span = TextUtils::wordSpanAt(translated, huangIndex);
    QVERIFY(span.valid());

    // The clicked span acts only as an anchor; the replacement target from
    // the candidate response may cover a longer run than the clicked word.
    const QString target = QStringLiteral("皇帝");
    const int start = TextUtils::nearestOccurrence(translated, target, span.start);
    QCOMPARE(start, span.start);

    QString replaced = translated;
    replaced.replace(start, target.size(), QStringLiteral("君主"));
    QCOMPARE(replaced,
             QStringLiteral("莫卧儿君主是从什么时候开始觉得自己是印度人的？"));

    // Anchoring resolves to the occurrence closest to the given position.
    const QString repeated = QStringLiteral("alpha beta alpha");
    QCOMPARE(TextUtils::nearestOccurrence(repeated, QStringLiteral("alpha"), 6), 11);
    QCOMPARE(TextUtils::nearestOccurrence(repeated, QStringLiteral("gamma"), 0), -1);
    QCOMPARE(TextUtils::nearestOccurrence(repeated, QString(), 0), -1);
}

void TestCore::singleInstanceArbitration()
{
    {
        SingleInstance first;
        QCOMPARE(first.tryLock(), SingleInstance::Role::Primary);
        QVERIFY(first.isPrimary());

        {
            SingleInstance second;
            QCOMPARE(second.tryLock(), SingleInstance::Role::Secondary);
            QVERIFY(!second.isPrimary());

            QSignalSpy activated(&first, &SingleInstance::activationRequested);
            second.notifyExistingInstance();
            QVERIFY(activated.wait(2000));
            QCOMPARE(activated.count(), 1);
        }
    }

    // A fresh launch takes over once the previous primary released the lock,
    // which also covers takeover after a crashed instance.
    SingleInstance third;
    QCOMPARE(third.tryLock(), SingleInstance::Role::Primary);
    QVERIFY(third.isPrimary());
}

void TestCore::guessFromScriptDetection()
{
    using namespace Languages;
    QCOMPARE(guessFromScript(QStringLiteral("你好，世界")), QStringLiteral("zh"));
    QCOMPARE(guessFromScript(QStringLiteral("こんにちは世界")), QStringLiteral("ja"));
    QCOMPARE(guessFromScript(QStringLiteral("안녕하세요")), QStringLiteral("ko"));
    QCOMPARE(guessFromScript(QStringLiteral("Привет, мир")), QStringLiteral("ru"));
    QCOMPARE(guessFromScript(QStringLiteral("مرحبا بالعالم")), QStringLiteral("ar"));
    QCOMPARE(guessFromScript(QStringLiteral("שלום עולם")), QStringLiteral("he"));
    QCOMPARE(guessFromScript(QStringLiteral("สวัสดีครับ")), QStringLiteral("th"));
    QCOMPARE(guessFromScript(QStringLiteral("Hello world")), QString());
    QCOMPARE(guessFromScript(QString()), QString());
    QCOMPARE(guessFromScript(QStringLiteral("Hello 你好")), QStringLiteral("zh"));
}

void TestCore::scriptDetectionCoversSupplementaryPlanes()
{
    using namespace Languages;
    // Supplementary-plane ideographs are classified through their surrogate
    // pair rather than by inspecting the packed UTF-16 code unit.
    QCOMPARE(guessFromScript(QString::fromUcs4(U"\U00020000", 1)), QStringLiteral("zh"));
    QCOMPARE(guessFromScript(QString::fromUcs4(U"\U0002A6DF", 1)), QStringLiteral("zh"));
    QCOMPARE(guessFromScript(QString::fromUcs4(U"\U00020000\U00020001", 2)), QStringLiteral("zh"));
    // Hangul Jamo Extended-A, which a code-point range table that stops at the
    // Hangul syllables fails to cover.
    QCOMPARE(guessFromScript(QStringLiteral("\uA960\uA961")), QStringLiteral("ko"));
    // Halfwidth katakana and Katakana Phonetic Extensions are both Japanese.
    QCOMPARE(guessFromScript(QStringLiteral("\uFF66\uFF67")), QStringLiteral("ja"));
    QCOMPARE(guessFromScript(QStringLiteral("\u31F0\u31F1")), QStringLiteral("ja"));

    // Unassigned code points inside an assigned block are not a script.
    QCOMPARE(guessFromScript(QStringLiteral("\u0B80\u0B81")), QString());
    // Shared punctuation carries no script of its own.
    QCOMPARE(guessFromScript(QStringLiteral("\u3001\u3002")), QString());

    // A word-span lookup over supplementary ideographs stays within the run.
    const QString extB = QString::fromUcs4(U"\U00020000\U00020001\U00020002", 3);
    const TextUtils::WordSpan span = TextUtils::wordSpanAt(extB, 0);
    QVERIFY(span.valid());
    QVERIFY(span.length() <= 8);
}

void TestCore::resolveAutoExcludesTarget()
{
    using namespace Languages;
    QCOMPARE(resolveAuto(QStringLiteral("你好"), QStringLiteral("en")), QStringLiteral("zh"));
    QCOMPARE(resolveAuto(QStringLiteral("你好"), QStringLiteral("fr")), QStringLiteral("zh"));
    const QString latinFallback = resolveAuto(QStringLiteral("Hello world"), QStringLiteral("en"));
    QVERIFY(latinFallback != QStringLiteral("en"));
    QVERIFY(indexOf(latinFallback) > 0);
    QVERIFY(resolveAuto(QStringLiteral("你好"), QStringLiteral("zh")) != QStringLiteral("zh"));
    QVERIFY(resolveAuto(QString(), QStringLiteral("ja")) != QStringLiteral("ja"));
}

void TestCore::requestBodyParameterHandling()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());

    QCOMPARE(Defaults::apiTemperature, -0.1);
    const QJsonObject body = TranslationEngine::buildRequestBody(QStringLiteral("Hello"), false);
    QVERIFY(!body.contains(QStringLiteral("temperature")));

    ConfigManager::instance()->setValue(Keys::apiTemperature, 0.7);
    const QJsonObject tuned = TranslationEngine::buildRequestBody(QStringLiteral("Hello"), false);
    QCOMPARE(tuned.value(QStringLiteral("temperature")).toDouble(), 0.7);
}

void TestCore::chatCompletionsUrlDerivation()
{
    const auto url = [](const char* base) {
        return ApiClient::chatCompletionsUrl(QString::fromLatin1(base)).toString();
    };
    QCOMPARE(url("https://api.openai.com/v1"),
             QStringLiteral("https://api.openai.com/v1/chat/completions"));
    QCOMPARE(url("https://api.openai.com/v1/"),
             QStringLiteral("https://api.openai.com/v1/chat/completions"));
    // Redundant trailing slashes collapse instead of doubling in the path.
    QCOMPARE(url("https://api.openai.com/v1///"),
             QStringLiteral("https://api.openai.com/v1/chat/completions"));
    QCOMPARE(url("http://localhost:11434"),
             QStringLiteral("http://localhost:11434/chat/completions"));
    QCOMPARE(url("http://localhost:11434/"),
             QStringLiteral("http://localhost:11434/chat/completions"));
    QCOMPARE(url("https://example.com/a/b/"),
             QStringLiteral("https://example.com/a/b/chat/completions"));

    QCOMPARE(ApiClient::normalizedBaseUrl(QStringLiteral("https://api.example.com/v1///")).path(),
             QStringLiteral("/v1"));
}

void TestCore::customHeaderLineParsing()
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

void TestCore::configValueEqualityMatchesDefaults()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());
    ConfigManager* config = ConfigManager::instance();

    // Non-default values, including nested ones, are detected as changes.
    config->setValue(Keys::translationCustomTones,
                     QJsonArray{QJsonObject{{QStringLiteral("key"), QStringLiteral("x")}}});
    QVERIFY(!config->isDefault(Keys::translationCustomTones));
    // A structurally identical value compares equal to the loaded one.
    config->setValue(Keys::translationCustomTones, config->value(Keys::translationCustomTones));
    QCOMPARE(config->value(Keys::translationCustomTones).toArray().size(), 1);

    // Restoring a default drops the stored override again.
    config->setValue(Keys::translationCustomTones, QJsonArray());
    QVERIFY(config->isDefault(Keys::translationCustomTones));

    // Integer and equal-valued double representations stay interchangeable.
    config->setValue(Keys::uiFontSize, 14);
    QCOMPARE(config->value(Keys::uiFontSize).toInt(), 14);
    QVERIFY(!config->isDefault(Keys::uiFontSize));
    config->setValue(Keys::uiFontSize, 10);
    QVERIFY(config->isDefault(Keys::uiFontSize));

    // Values of different JSON types are never treated as equal.
    config->setValue(Keys::apiKey, QJsonValue(QStringLiteral("10")));
    QVERIFY(!config->isDefault(Keys::apiKey));
}

void TestCore::stopCancelsActiveRequest()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());

    // A listening server that never answers keeps the request in flight.
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    ConfigManager::instance()->setValue(Keys::apiBaseUrl,
        QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));

    TranslationEngine engine;
    QSignalSpy stoppedSpy(&engine, &TranslationEngine::stopped);
    QSignalSpy errorSpy(&engine, &TranslationEngine::error);
    QSignalSpy stateSpy(&engine, &TranslationEngine::stateChanged);

    TranslationContext context;
    context.sourceText = QStringLiteral("Hello");
    context.targetLang = QStringLiteral("zh");
    context.uiLanguage = QStringLiteral("en");
    engine.translateText(context);
    QVERIFY(engine.busy());

    engine.stop();

    QCOMPARE(stoppedSpy.count(), 1);
    QCOMPARE(errorSpy.count(), 0);
    QVERIFY(!engine.busy());
    QVERIFY(!stateSpy.isEmpty());
    QCOMPARE(stateSpy.last().last().toBool(), false);
}

void TestCore::failedDispatchReturnsToIdle()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());
    ConfigManager::instance()->setValue(Keys::apiBaseUrl, QString());

    TranslationEngine engine;
    QSignalSpy stoppedSpy(&engine, &TranslationEngine::stopped);
    QSignalSpy errorSpy(&engine, &TranslationEngine::error);
    QSignalSpy stateSpy(&engine, &TranslationEngine::stateChanged);

    TranslationContext context;
    context.sourceText = QStringLiteral("Hello");
    context.targetLang = QStringLiteral("zh");
    context.uiLanguage = QStringLiteral("en");
    engine.translateText(context);

    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(!errorSpy.first().first().toString().isEmpty());
    QCOMPARE(stoppedSpy.count(), 0);
    QVERIFY(!engine.busy());
    QCOMPARE(stateSpy.last().last().toBool(), false);
}

void TestCore::stopWhenIdleIsNoOp()
{
    TranslationEngine engine;
    QSignalSpy stoppedSpy(&engine, &TranslationEngine::stopped);
    QSignalSpy stateSpy(&engine, &TranslationEngine::stateChanged);

    engine.stop();

    QCOMPARE(stoppedSpy.count(), 0);
    QVERIFY(!engine.busy());
    QVERIFY(stateSpy.isEmpty());
}

QTEST_MAIN(TestCore)
#include "test_core.moc"
