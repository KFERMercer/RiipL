#include <QtTest>

#include "core/config/ApiPreset.h"
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
    void promptReferenceBlockIsDynamic();
    void promptGlossaryFormatting();
    void promptReferenceEntryKeepsBodyIntact();
    void candidatePromptSubstitution();
    void candidateShortPromptSubstitution();
    void everyTemplateReachesEveryPlaceholder();
    void knownPlaceholdersCoverVariables();
    void glossaryRoundTrip();
    void historyTrimming();
    void historyDebouncesSavesUntilFlush();
    void uiLanguageResolution();
    void wordSpanAtBoundaries();
    void candidateResponseParsing();
    void candidateFragmentWindow();
    void candidateWindowCountsWords();
    void candidateResolution();
    void replaceTargetsCompleteWord();
    void replacementAbsorbsRestatedNeighbour();
    void singleInstanceArbitration();
    void guessFromScriptDetection();
    void scriptDetectionCoversSupplementaryPlanes();
    void scriptDetectionIgnoresScriptlessAttachments();
    void wordSpanCountsSupplementaryIdeographs();
    void resolveAutoExcludesTarget();
    void requestBodyParameterHandling();
    void customHeaderLineParsing();
    void baseUrlNormalization();
    void requestTargetsDerivedEndpoint();
    void configValueEqualityMatchesDefaults();
    void apiPresetRoundTrip();
    void apiPresetLookupAndMatching();
    void apiPresetMatchesAppliedSettings();
    void apiPresetApplyWritesEveryField();
    void stopCancelsActiveRequest();
    void candidateRequestRetriesEmptyReply();
    void candidateRequestCarriesSystemPrompt();
    void candidateRequestPicksShortTextTemplate();
    void failedDispatchReturnsToIdle();
    void stopWhenIdleIsNoOp();

private:
    static QStringList optionTexts(const TranslationEngine::CandidateGroup& group)
    {
        QStringList texts;
        for (const TranslationEngine::CandidateOption& option : group.options)
            texts << option.text;
        return texts;
    }

    QString tempDir()
    {
        static QTemporaryDir dir;
        return dir.path() + QStringLiteral("/%1").arg(QTest::currentTestFunction());
    }

    // Content of the user message in a recorded chat-completions request body.
    static QString requestUserPrompt(const QByteArray& body)
    {
        const QJsonArray messages = QJsonDocument::fromJson(body).object()
                                        .value(QStringLiteral("messages")).toArray();
        for (const QJsonValue& message : messages) {
            const QJsonObject object = message.toObject();
            if (object.value(QStringLiteral("role")).toString() == QLatin1String("user"))
                return object.value(QStringLiteral("content")).toString();
        }
        return QString();
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

void TestCore::promptReferenceBlockIsDynamic()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());

    TranslationContext context;
    context.sourceText = QStringLiteral("Hello");
    context.targetLang = QStringLiteral("zh");
    context.uiLanguage = QStringLiteral("zh");

    // No option set: the reference block is omitted entirely, so unset items
    // cannot pollute the prompt.
    QString prompt = PromptBuilder::build(context).user;
    QVERIFY(!prompt.contains(Defaults::promptReferenceZh.trimmed()));
    QVERIFY(!prompt.contains(QStringLiteral("- 语气：")));
    QVERIFY(!prompt.contains(QStringLiteral("- 风格：")));
    QVERIFY(!prompt.contains(QStringLiteral("- 背景信息：")));
    QVERIFY(!prompt.contains(QStringLiteral("- 术语表：")));

    // The neutral tone is the absence of a tone, not a reference item.
    context.tone = QStringLiteral("neutral");
    prompt = PromptBuilder::build(context).user;
    QVERIFY(!prompt.contains(Defaults::promptReferenceZh.trimmed()));
    QVERIFY(!prompt.contains(QStringLiteral("- 语气：")));

    // Each set option appends exactly its own entry, in reference order.
    context.tone = QStringLiteral("formal");
    context.style = QStringLiteral("concise");
    context.background = QStringLiteral("deployment notes");
    prompt = PromptBuilder::build(context).user;
    QVERIFY(prompt.startsWith(Defaults::promptReferenceZh.trimmed()));
    QVERIFY(prompt.contains(QStringLiteral("formal")));
    QVERIFY(prompt.contains(QStringLiteral("concise")));
    QVERIFY(prompt.contains(QStringLiteral("deployment notes")));
    const int toneAt = prompt.indexOf(QStringLiteral("- 语气："));
    const int styleAt = prompt.indexOf(QStringLiteral("- 风格："));
    const int backgroundAt = prompt.indexOf(QStringLiteral("- 背景信息："));
    QVERIFY(toneAt >= 0 && styleAt > toneAt && backgroundAt > styleAt);

    // The instruction follows the reference block.
    QVERIFY(prompt.indexOf(QStringLiteral("Hello")) > backgroundAt);

    // A disabled or empty glossary contributes no entry.
    TranslationContext glossaryContext;
    glossaryContext.sourceText = QStringLiteral("Hello");
    glossaryContext.targetLang = QStringLiteral("en");
    glossaryContext.uiLanguage = QStringLiteral("zh");
    glossaryContext.glossary = {{QStringLiteral("苹果"), QStringLiteral("Apple")}};
    glossaryContext.glossaryEnabled = false;
    QVERIFY(!PromptBuilder::build(glossaryContext).user.contains(QStringLiteral("- 术语表：")));
    glossaryContext.glossaryEnabled = true;
    QVERIFY(PromptBuilder::build(glossaryContext).user.contains(QStringLiteral("- 术语表：")));
}

void TestCore::promptGlossaryFormatting()
{
    QVector<GlossaryEntry> entries;
    entries.append({QStringLiteral("苹果"), QStringLiteral("Apple")});
    entries.append({QStringLiteral("RiipL"), QString()});

    const QString data = PromptBuilder::glossaryData(entries, QStringLiteral("zh"));
    const QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    QVERIFY(doc.isArray());
    const QJsonArray array = doc.array();
    QCOMPARE(array.size(), 2);
    QCOMPARE(array.at(0).toObject().value(QStringLiteral("原文")).toString(), QStringLiteral("苹果"));
    QCOMPARE(array.at(0).toObject().value(QStringLiteral("译文")).toString(), QStringLiteral("Apple"));
    QCOMPARE(array.at(1).toObject().value(QStringLiteral("原文")).toString(), QStringLiteral("RiipL"));
    // A term without a target is mapped to itself, which states "keep as-is"
    // without a null literal that some models echo verbatim.
    QCOMPARE(array.at(1).toObject().value(QStringLiteral("译文")).toString(), QStringLiteral("RiipL"));

    // Entries without a source term carry no information and are dropped.
    QVERIFY(PromptBuilder::glossaryData({{QString(), QStringLiteral("Apple")}},
                                        QStringLiteral("zh")).isEmpty());

    TranslationContext context;
    context.sourceText = QStringLiteral("苹果 is good");
    context.targetLang = QStringLiteral("en");
    context.glossary = entries;
    context.uiLanguage = QStringLiteral("zh");
    PromptBuilder::Result result = PromptBuilder::build(context);
    QVERIFY(result.user.contains(QStringLiteral("- 术语表：")));
    QVERIFY(result.user.contains(QStringLiteral("```json")));
    QVERIFY(result.user.contains(QStringLiteral("\"原文\": \"苹果\"")));

    // The glossary keys follow the UI language of the template.
    context.uiLanguage = QStringLiteral("en");
    result = PromptBuilder::build(context);
    QVERIFY(result.user.contains(QStringLiteral("\"source\": \"苹果\"")));
    QVERIFY(result.user.contains(QStringLiteral("\"target\": \"Apple\"")));
}

void TestCore::promptReferenceEntryKeepsBodyIntact()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());

    // Labels and fences live in the templates. A multi-line value inherits the
    // indentation of the placeholder line, so the block stays aligned.
    TranslationContext context;
    context.sourceText = QStringLiteral("Hello");
    context.targetLang = QStringLiteral("de");
    context.uiLanguage = QStringLiteral("zh");
    context.tone = QStringLiteral("formal");
    context.style = QStringLiteral("concise\ntechnical");
    context.background = QStringLiteral("line one\nline two");
    context.glossaryEnabled = true;
    context.glossary = {{QStringLiteral("fox"), QStringLiteral("Fuchs")}};

    const QString prompt = PromptBuilder::build(context).user;
    QVERIFY(prompt.contains(QStringLiteral("- 语气：\n  ```\n  formal\n  ```")));
    QVERIFY(prompt.contains(QStringLiteral("- 风格：\n  ```\n  concise\n  technical\n  ```")));
    QVERIFY(prompt.contains(QStringLiteral("- 背景信息：\n  ```\n  line one\n  line two\n  ```")));
    QVERIFY(prompt.contains(QStringLiteral("- 术语表：\n  ```json\n  [\n      {\n          \"原文\": \"fox\",")));
    // One shared header and the instruction last.
    QCOMPARE(prompt.count(Defaults::promptReferenceZh.trimmed()), 1);
    QVERIFY(prompt.indexOf(QStringLiteral("Hello")) > prompt.indexOf(Defaults::promptReferenceZh.trimmed()));

    // A single-line value keeps the template's own indentation untouched.
    TranslationContext single = context;
    single.style = QStringLiteral("concise");
    QVERIFY(PromptBuilder::build(single).user.contains(QStringLiteral("  concise")));

    // An unset option removes its whole entry, indentation included.
    single.style.clear();
    QVERIFY(!PromptBuilder::build(single).user.contains(QStringLiteral("- 风格：")));
}


void TestCore::candidatePromptSubstitution()
{
    const QString fragment = QStringLiteral("[[世界]]，你好");
    const QString full = QStringLiteral("你好，世界！今天天气不错。");
    TranslationContext context;
    context.translatedText = full;
    context.selectedFragment = fragment;
    context.selectedWord = QStringLiteral("世界");
    context.targetLang = QStringLiteral("zh");
    context.uiLanguage = QStringLiteral("en");

    const QString prompt = PromptBuilder::candidatePrompt(Prompts::candidateTemplate, context);
    QVERIFY(prompt.contains(fragment));
    QVERIFY(prompt.contains(QStringLiteral("世界")));
    QVERIFY(prompt.contains(QStringLiteral("Chinese")));
    QVERIFY(prompt.contains(QStringLiteral("JSON")));
    QVERIFY(!prompt.contains(QStringLiteral("{target_lang}")));
    QVERIFY(!prompt.contains(QStringLiteral("{selected_fragment}")));
    QVERIFY(!prompt.contains(QStringLiteral("{mark_left}")));
    QVERIFY(!prompt.contains(QStringLiteral("{mark_right}")));
    // The rendered markers are the very ones the engine wraps the selection
    // with, so the prompt and the renderer cannot drift apart.
    QVERIFY(prompt.contains(CandidateMarks::selectionOpen));
    QVERIFY(prompt.contains(CandidateMarks::selectionClose));

    // The fragment is fenced with backticks, so the brackets that mark the
    // selection cannot be confused with the fence or with JSON syntax.
    QVERIFY(prompt.contains(QStringLiteral("```\n%1\n```").arg(fragment)));

    // {translated_text} stays the full translation, which this template does not
    // reach for, so the request carries the marked window alone.
    QVERIFY(!prompt.contains(full));
}

// Every template is offered the same placeholders, so a wording prompt can reach
// the tone, the style and the glossary just as a translation prompt can, and a
// translation prompt can reach the selection.
void TestCore::everyTemplateReachesEveryPlaceholder()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());

    TranslationContext context;
    context.sourceLang = QStringLiteral("en");
    context.targetLang = QStringLiteral("zh");
    context.tone = QStringLiteral("formal");
    context.style = QStringLiteral("concise");
    context.background = QStringLiteral("deployment notes");
    context.glossary = {{QStringLiteral("fox"), QStringLiteral("狐狸")}};
    context.glossaryEnabled = true;
    context.uiLanguage = QStringLiteral("zh");
    context.sourceText = QStringLiteral("The fox jumps.");
    context.translatedText = QStringLiteral("狐狸在跳。");
    context.selectedWord = QStringLiteral("狐狸");
    context.selectedFragment = QStringLiteral("[[狐狸]]在跳。");

    // A template naming every placeholder renders each of them to a value.
    QStringList tokens;
    for (const QString& placeholder : PromptBuilder::knownPlaceholders())
        tokens << QStringLiteral("{%1}").arg(placeholder);
    const QString probe = QStringLiteral("[%1]").arg(tokens.join(QStringLiteral("][")));
    const QStringList templates = {
        Prompts::systemTemplate, Prompts::referenceTemplate, Prompts::toneTemplate,
        Prompts::styleTemplate, Prompts::backgroundTemplate, Prompts::glossaryTemplate,
        Prompts::defaultTemplate, Prompts::candidateTemplate, Prompts::candidateShortTemplate
    };
    for (const QString& name : templates) {
        ConfigManager::instance()->setValue(
            Keys::promptKey(name, QStringLiteral("zh")), probe);
        const QString rendered = PromptBuilder::candidatePrompt(name, context);
        for (const QString& token : tokens)
            QVERIFY2(!rendered.contains(token), qPrintable(rendered));
        for (const QString& value : {context.sourceText, context.translatedText,
                                     context.selectedWord, context.selectedFragment,
                                     context.tone, context.style, context.background}) {
            QVERIFY2(rendered.contains(value), qPrintable(rendered));
        }
        // The glossary renders as JSON, so it is checked as data rather than as
        // the literal text it was configured with.
        QVERIFY(rendered.contains(QStringLiteral("\"fox\"")));
        QVERIFY(rendered.contains(CandidateMarks::selectionOpen));
        QVERIFY(rendered.contains(CandidateMarks::selectionClose));
    }

    // An option the context leaves unset still renders away rather than failing.
    TranslationContext bare;
    bare.translatedText = QStringLiteral("text");
    bare.uiLanguage = QStringLiteral("zh");
    const QString rendered = PromptBuilder::candidatePrompt(Prompts::candidateTemplate, bare);
    for (const QString& token : tokens)
        QVERIFY2(!rendered.contains(token), qPrintable(rendered));
}

void TestCore::candidateShortPromptSubstitution()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());

    TranslationContext context;
    context.translatedText = QStringLiteral("你好，世界！");
    context.selectedWord = QStringLiteral("世界");
    context.selectedFragment = QStringLiteral("你好，[[世界]]！");
    context.sourceText = QStringLiteral("Hello, world!");
    context.targetLang = QStringLiteral("zh");
    context.uiLanguage = QStringLiteral("en");

    const QString prompt =
        PromptBuilder::candidatePrompt(Prompts::candidateShortTemplate, context);

    // The short-text template is rendered from the marked window, which is the
    // whole translation here, and carries the source text next to it.
    QVERIFY(prompt.contains(context.selectedFragment));
    QVERIFY(prompt.contains(context.sourceText));
    QVERIFY(prompt.contains(QStringLiteral("世界")));
    QVERIFY(prompt.contains(QStringLiteral("Chinese")));
    QVERIFY(prompt.contains(CandidateMarks::selectionOpen));
    QVERIFY(prompt.contains(CandidateMarks::selectionClose));
    for (const QString& placeholder : {QStringLiteral("{target_lang}"),
                                       QStringLiteral("{translated_text}"),
                                       QStringLiteral("{selected_fragment}"),
                                       QStringLiteral("{source_text}"),
                                       QStringLiteral("{selected_word}"),
                                       QStringLiteral("{mark_left}"),
                                       QStringLiteral("{mark_right}")}) {
        QVERIFY(!prompt.contains(placeholder));
    }

    // A configured template overrides the built-in one, like every other prompt.
    ConfigManager::instance()->setValue(Keys::promptCandidateShortZh,
                                        QStringLiteral("短文本：{source_text}"));
    ConfigManager::instance()->setValue(Keys::uiLanguage, QStringLiteral("zh"));
    context.uiLanguage = ConfigManager::instance()->resolvedUiLanguage();
    QCOMPARE(PromptBuilder::candidatePrompt(Prompts::candidateShortTemplate, context),
             QStringLiteral("短文本：Hello, world!"));
}

void TestCore::knownPlaceholdersCoverVariables()
{
    const QStringList placeholders = PromptBuilder::knownPlaceholders();
    QCOMPARE(placeholders.size(), QSet<QString>(placeholders.cbegin(), placeholders.cend()).size());

    // The order is the presentation order of the placeholder chips in settings.
    const QStringList expected = {
        QStringLiteral("source_lang"), QStringLiteral("target_lang"), QStringLiteral("tone"),
        QStringLiteral("style"), QStringLiteral("background"), QStringLiteral("glossary"),
        QStringLiteral("source_text"), QStringLiteral("translated_text"),
        QStringLiteral("selected_fragment"), QStringLiteral("selected_word"),
        QStringLiteral("mark_left"), QStringLiteral("mark_right")
    };
    QCOMPARE(placeholders, expected);

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
        "[{\"old\": \"皇帝\", \"new\": [\"君主\", \"帝王\"]}, "
        "{\"old\": \"莫卧儿\", \"new\": [\"蒙兀儿\"]}]");
    const QVector<TranslationEngine::CandidateGroup> groups =
        TranslationEngine::parseCandidateResponse(json);
    QCOMPARE(groups.size(), 2);
    QCOMPARE(groups.at(0).target, QStringLiteral("皇帝"));
    QCOMPARE(optionTexts(groups.at(0)), QStringList({QStringLiteral("君主"), QStringLiteral("帝王")}));
    QCOMPARE(groups.at(1).target, QStringLiteral("莫卧儿"));

    // A fenced reply parses the same way.
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
    QCOMPARE(optionTexts(parsed.at(0)), QStringList({QStringLiteral("君主")}));
    QCOMPARE(optionTexts(parsed.at(1)), QStringList({QStringLiteral("蒙兀儿")}));

    QVERIFY(TranslationEngine::parseCandidateResponse(QStringLiteral("没有 JSON")).isEmpty());
}

void TestCore::candidateFragmentWindow()
{
    const QString text = QStringLiteral(
        "First sentence with plenty of words in it. Second sentence carries the target word here. "
        "Third sentence closes the paragraph.");

    const int target = text.indexOf(QStringLiteral("target"));
    QVERIFY(target > 0);
    const TextUtils::WordSpan word = TextUtils::wordSpanAt(text, target);
    QVERIFY(word.valid());
    QCOMPARE(text.mid(word.start, word.length()), QStringLiteral("target"));

    // Without surrounding words the fragment is the selection itself.
    const TextUtils::Fragment tight =
        TextUtils::candidateFragment(text, word.start, word.end, {0, 0});
    QVERIFY(tight.valid());
    QCOMPARE(tight.text, QStringLiteral("target"));
    QCOMPARE(tight.markStart, 0);
    QCOMPARE(tight.markEnd, word.length());

    // The window reaches exactly as many words as it is granted on each side,
    // and its edges sit on whole words.
    const TextUtils::Fragment wide =
        TextUtils::candidateFragment(text, word.start, word.end, {3, 2});
    QVERIFY(wide.valid());
    QCOMPARE(wide.text, QStringLiteral("sentence carries the target word here"));
    QCOMPARE(wide.text.mid(wide.markStart, wide.markEnd - wide.markStart),
             QStringLiteral("target"));
    QVERIFY(!wide.text.front().isSpace() && !wide.text.back().isSpace());

    // A window wider than the text still stops at its edges, which is what lets
    // the caller tell a window over a short translation from a local one.
    const TextUtils::Fragment all =
        TextUtils::candidateFragment(text, word.start, word.end, {100, 100});
    QVERIFY(all.valid());
    QCOMPARE(all.text, text);
    QVERIFY(all.text != wide.text);
}

void TestCore::candidateWindowCountsWords()
{
    // A word of context is one segment of the word break rules, so punctuation
    // the editor selects on its own spends one of them and a blank run spends
    // none.
    const QString english = QStringLiteral("Hello, world! It's fine.");
    const TextUtils::WordSpan world = TextUtils::wordSpanAt(english, english.indexOf(QStringLiteral("world")));
    QVERIFY(world.valid());
    const auto window = [&](int before, int after) {
        return TextUtils::candidateFragment(english, world.start, world.end, {before, after}).text;
    };
    QCOMPARE(window(1, 0), QStringLiteral(", world"));
    QCOMPARE(window(2, 0), QStringLiteral("Hello, world"));
    QCOMPARE(window(0, 1), QStringLiteral("world!"));
    QCOMPARE(window(1, 1), QStringLiteral(", world!"));
    QCOMPARE(window(0, 2), QStringLiteral("world! It's"));
    QCOMPARE(window(9, 9), english);

    // The same counting drives a text without blanks, where every ideograph is
    // its own word and the full-width comma is one too.
    const QString chinese = QStringLiteral("你好，世界！");
    const TextUtils::WordSpan shi = TextUtils::wordSpanAt(chinese, chinese.indexOf(QStringLiteral("世")));
    QVERIFY(shi.valid());
    QCOMPARE(chinese.mid(shi.start, shi.length()), QStringLiteral("世"));
    QCOMPARE(TextUtils::candidateFragment(chinese, shi.start, shi.end, {2, 0}).text,
             QStringLiteral("好，世"));
    QCOMPARE(TextUtils::candidateFragment(chinese, shi.start, shi.end, {0, 1}).text,
             QStringLiteral("世界"));
    QCOMPARE(TextUtils::candidateFragment(chinese, shi.start, shi.end, {10, 30}).text,
             chinese);

    // A CJK run the word break rules split per character is counted per
    // character, which is what the editor hands out there.
    const QString run = QStringLiteral("\u8fd9\u662f\u4e00\u6bb5\u6ca1\u6709\u4efb\u4f55\u6807\u70b9\u7684\u5f88\u957f\u4e2d\u6587\u6587\u672c");
    const TextUtils::Fragment local = TextUtils::candidateFragment(run, 5, 6, {1, 1});
    QVERIFY(local.valid());
    QCOMPARE(local.text, run.mid(4, 3));
}

void TestCore::candidateResolution()
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
                                        QStringLiteral("the old story"), {}).valid());

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
                                        QStringLiteral("aa"), {}).valid());

    QVERIFY(!TextUtils::replacementSpan(text, manAt + 4, manAt + 7, QString(), {}).valid());

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

void TestCore::replaceTargetsCompleteWord()
{
    const QString translated = QStringLiteral("莫卧儿皇帝是从什么时候开始觉得自己是印度人的？");
    const int huangIndex = translated.indexOf(QStringLiteral("皇"));
    const TextUtils::WordSpan span = TextUtils::wordSpanAt(translated, huangIndex);
    QVERIFY(span.valid());

    // The clicked span acts only as an anchor; the target from the candidate
    // response may cover a longer run than the clicked word, and it resolves
    // back to the absolute span each replacement is applied to.
    const QString target = QStringLiteral("皇帝");
    const TextUtils::WordSpan resolved = TextUtils::replacementSpan(
        translated, span.start, span.end, target, {QStringLiteral("君主")});
    QVERIFY(resolved.valid());
    QCOMPARE(resolved.start, span.start);
    QCOMPARE(translated.mid(resolved.start, resolved.length()), target);

    QString replaced = translated;
    replaced.replace(resolved.start, resolved.length(), QStringLiteral("君主"));
    QCOMPARE(replaced,
             QStringLiteral("莫卧儿君主是从什么时候开始觉得自己是印度人的？"));
}

void TestCore::replacementAbsorbsRestatedNeighbour()
{
    const QString text = QStringLiteral("傍晚时，小猫带着一桶鱼开心地回家了。");
    const int cat = text.indexOf(QStringLiteral("小猫"));
    QVERIFY(cat >= 0);

    // The reported defect: the click lands on the second character of a word the
    // model narrows to that character, and the alternative it proposes restates
    // the first one. Taking `old` literally would splice `小猫咪` after the `小`
    // that was left behind and produce `小小猫咪`; the span has to cover `小猫`.
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
    // Scriptless code points are not counted, so a lone punctuation mark or
    // combining mark carries no language.
    QCOMPARE(guessFromScript(QStringLiteral("\u3001")), QString());
    QCOMPARE(guessFromScript(QStringLiteral("\u060C")), QString());
    QCOMPARE(guessFromScript(QStringLiteral("\u064B\u064C\u064D")), QString());
    QCOMPARE(guessFromScript(QStringLiteral("\u30FC")), QString());
    QCOMPARE(guessFromScript(QStringLiteral("   ")), QString());
}

void TestCore::scriptDetectionIgnoresScriptlessAttachments()
{
    using namespace Languages;
    // Punctuation and combining marks attached to script-bearing letters do not
    // outvote them, so fully vocalized text keeps its language.
    QCOMPARE(guessFromScript(QStringLiteral("\u0628\u0650\u0633\u0652\u0645\u0650 \u0627\u0644\u0644\u0651\u064e\u0647\u0650")),
             QStringLiteral("ar"));
    QCOMPARE(guessFromScript(QStringLiteral("\u0928\u092e\u0938\u094d\u0924\u0947 \u0926\u0941\u0928\u093f\u092f\u093e")),
             QStringLiteral("hi"));
    QCOMPARE(guessFromScript(QStringLiteral("\u0e2a\u0e27\u0e31\u0e2a\u0e14\u0e35\u0e04\u0e23\u0e31\u0e1a")),
             QStringLiteral("th"));
    QCOMPARE(guessFromScript(QStringLiteral("\u30ab\u30bf\u30ab\u30ca\u30fb\u30c6\u30ad\u30b9\u30c8")), QStringLiteral("ja"));
    QCOMPARE(guessFromScript(QStringLiteral("\u4e2d\u6587\u3002\u6d4b\u8bd5")), QStringLiteral("zh"));
    QCOMPARE(guessFromScript(QStringLiteral("\u0645\u0631\u062d\u0628\u0627 abc")), QStringLiteral("ar"));
}

void TestCore::wordSpanCountsSupplementaryIdeographs()
{
    const QString ideograph = QString::fromUcs4(U"\U00020000", 1);

    // One supplementary ideograph occupies two UTF-16 units; classifying by
    // unit rather than by code point misreports the run length.
    const TextUtils::WordSpan single = TextUtils::wordSpanAt(ideograph, 0);
    QVERIFY(single.valid());
    QCOMPARE(single.length(), 2);

    const QString run = ideograph.repeated(3);
    const TextUtils::WordSpan first = TextUtils::wordSpanAt(run, 0);
    QVERIFY(first.valid());
    QCOMPARE(run.mid(first.start, first.length()), ideograph);

    // Hangul Jamo Extended-A carries the Hangul script, so a run of nine is
    // rejected for length exactly as nine Hangul syllables are. The code-unit
    // range table did not treat it as CJK and accepted the run instead.
    QCOMPARE(TextUtils::wordSpanAt(QStringLiteral("\uA960").repeated(8), 0).valid(), true);
    QCOMPARE(TextUtils::wordSpanAt(QStringLiteral("\uA960").repeated(9), 0).valid(), false);
    QCOMPARE(TextUtils::wordSpanAt(QStringLiteral("\u11A8").repeated(9), 0).valid(), false);
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
    const QJsonObject body = TranslationEngine::buildRequestBody(
        {QString(), QStringLiteral("Hello")}, false);
    QVERIFY(!body.contains(QStringLiteral("temperature")));

    ConfigManager::instance()->setValue(Keys::apiTemperature, 0.7);
    const QJsonObject tuned = TranslationEngine::buildRequestBody(
        {QString(), QStringLiteral("Hello")}, false);
    QCOMPARE(tuned.value(QStringLiteral("temperature")).toDouble(), 0.7);
}

void TestCore::baseUrlNormalization()
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
void TestCore::requestTargetsDerivedEndpoint()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());

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
    QString error;
    QJsonObject body;
    body.insert(QStringLiteral("stream"), false);
    client.sendChatRequest(body,
                           [&](const QString& text) { result = text; },
                           nullptr,
                           [&](const QString& message) { error = message; });

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result, QStringLiteral("ok"));
    QCOMPARE(requestLine, QStringLiteral("POST /v1/chat/completions HTTP/1.1"));
    QCOMPARE(authorization, QByteArrayLiteral("Bearer secret"));
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

    // A stored integer compares equal to the default that stores the same
    // number as a double, so the override is dropped instead of persisting.
    QCOMPARE(Defaults::value(Keys::uiFontSize).type(), QJsonValue::Double);
    config->setValue(Keys::uiFontSize, QJsonValue(int(Defaults::uiFontSize)));
    QVERIFY(config->isDefault(Keys::uiFontSize));
    QCOMPARE(config->value(Keys::uiFontSize).toInt(), Defaults::uiFontSize);

    config->setValue(Keys::uiFontSize, 14);
    QCOMPARE(config->value(Keys::uiFontSize).toInt(), 14);
    QVERIFY(!config->isDefault(Keys::uiFontSize));
    config->setValue(Keys::uiFontSize, Defaults::uiFontSize);
    QVERIFY(config->isDefault(Keys::uiFontSize));

    // Values of different JSON types are never treated as equal.
    config->setValue(Keys::apiKey, QJsonValue(QStringLiteral("10")));
    QVERIFY(!config->isDefault(Keys::apiKey));
}

void TestCore::apiPresetRoundTrip()
{
    ApiPreset preset;
    preset.name = QStringLiteral("OpenAI");
    preset.values.insert(Keys::apiBaseUrl, QStringLiteral("https://api.openai.com/v1"));
    preset.values.insert(Keys::apiModel, QStringLiteral("gpt-4o-mini"));
    preset.values.insert(Keys::apiMaxTokens, 2048);

    const QJsonArray encoded = ApiPresets::toJson({preset});
    const QVector<ApiPreset> restored = ApiPresets::fromJson(encoded);
    QCOMPARE(restored.size(), 1);
    QCOMPARE(restored.first().name, QStringLiteral("OpenAI"));
    QCOMPARE(restored.first().values.value(Keys::apiBaseUrl).toString(),
             QStringLiteral("https://api.openai.com/v1"));
    // Values keep their JSON types across the round trip.
    QCOMPARE(restored.first().values.value(Keys::apiMaxTokens).toInt(), 2048);

    // Entries without a usable name are dropped rather than kept as blanks.
    const QJsonArray malformed = QJsonArray{
        QJsonObject{{QStringLiteral("name"), QStringLiteral("  ")}},
        QJsonObject{{QStringLiteral("values"), QJsonObject()}},
        QJsonValue(42),
    };
    QCOMPARE(ApiPresets::fromJson(malformed).size(), 0);
    QCOMPARE(ApiPresets::fromJson(QJsonArray()).size(), 0);
}

void TestCore::apiPresetLookupAndMatching()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());

    ApiPreset openai;
    openai.name = QStringLiteral("OpenAI");
    openai.values.insert(Keys::apiBaseUrl, QStringLiteral("https://api.openai.com/v1"));
    openai.values.insert(Keys::apiKey, QStringLiteral("sk-openai"));
    openai.values.insert(Keys::apiModel, QStringLiteral("gpt-4o-mini"));

    ApiPreset local;
    local.name = QStringLiteral("Local");
    local.values.insert(Keys::apiBaseUrl, QStringLiteral("http://localhost:11434"));
    local.values.insert(Keys::apiModel, QStringLiteral("qwen2.5"));

    const QVector<ApiPreset> presets = {openai, local};
    QCOMPARE(ApiPresets::indexOf(presets, QStringLiteral("Local")), 1);
    QCOMPARE(ApiPresets::indexOf(presets, QStringLiteral("Missing")), -1);

    // A preset matches the settings only while every captured field agrees.
    QCOMPARE(ApiPresets::matchValues(presets, openai.values), 0);
    QJsonObject drifted = openai.values;
    drifted.insert(Keys::apiModel, QStringLiteral("gpt-4o"));
    QCOMPARE(ApiPresets::matchValues(presets, drifted), -1);
    // Changing a field no preset captured still breaks the match.
    drifted = openai.values;
    drifted.insert(Keys::apiTemperature, 0.5);
    QCOMPARE(ApiPresets::matchValues(presets, drifted), -1);

    // A field a preset does not carry is compared against its default, so an
    // omitted field does not make the preset match unconditionally.
    QCOMPARE(presets.at(1).values.contains(Keys::apiTemperature), false);
    QCOMPARE(ApiPresets::matchValues({local},
                                     QJsonObject{{Keys::apiBaseUrl, QStringLiteral("http://localhost:11434")},
                                                 {Keys::apiModel, QStringLiteral("qwen2.5")}}),
             0);
    QCOMPARE(ApiPresets::matchValues({local},
                                     QJsonObject{{Keys::apiBaseUrl, QStringLiteral("http://localhost:11434")},
                                                 {Keys::apiModel, QStringLiteral("qwen2.5")},
                                                 {Keys::apiTemperature, 0.7}}),
             -1);

    // Each captured field is compared by its own type, not by string form.
    ApiPreset typed;
    typed.name = QStringLiteral("Typed");
    typed.values.insert(Keys::apiMaxTokens, 4096);
    QCOMPARE(ApiPresets::matchValues({typed}, QJsonObject{{Keys::apiMaxTokens, 4096}}), 0);
    QCOMPARE(ApiPresets::matchValues({typed},
                                     QJsonObject{{Keys::apiMaxTokens, QStringLiteral("4096")}}),
             -1);

    QCOMPARE(ApiPresets::matchValues({}, openai.values), -1);
}

// The management window highlights the preset that the applied settings match,
// so matching against the configuration has to identify it exactly.
void TestCore::apiPresetMatchesAppliedSettings()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());
    ConfigManager* config = ConfigManager::instance();

    ApiPreset openai;
    openai.name = QStringLiteral("OpenAI");
    openai.values.insert(Keys::apiBaseUrl, QStringLiteral("https://api.openai.com/v1"));
    openai.values.insert(Keys::apiKey, QStringLiteral("sk-openai"));
    openai.values.insert(Keys::apiModel, QStringLiteral("gpt-4o-mini"));

    ApiPreset local;
    local.name = QStringLiteral("Local");
    local.values.insert(Keys::apiBaseUrl, QStringLiteral("http://localhost:11434"));
    local.values.insert(Keys::apiModel, QStringLiteral("qwen2.5"));

    const QVector<ApiPreset> presets = {openai, local};

    // Nothing applied yet: the defaults match no preset.
    QCOMPARE(ApiPresets::matchValues(presets, ApiPresets::capture()), -1);

    // Applying a preset makes capture() identify that very preset.
    ApiPresets::apply(local);
    QCOMPARE(ApiPresets::matchValues(presets, ApiPresets::capture()), 1);
    QCOMPARE(ApiPresets::capture().value(Keys::apiModel).toString(), QStringLiteral("qwen2.5"));

    ApiPresets::apply(openai);
    QCOMPARE(ApiPresets::matchValues(presets, ApiPresets::capture()), 0);

    // Editing one applied field breaks the match instead of staying ambiguous.
    config->setValue(Keys::apiModel, QStringLiteral("gpt-4o"));
    QCOMPARE(ApiPresets::matchValues(presets, ApiPresets::capture()), -1);

    // capture() reads every captured field, so a stray value in any of them is
    // reported rather than silently matching the preset.
    ApiPresets::apply(openai);
    QCOMPARE(ApiPresets::matchValues(presets, ApiPresets::capture()), 0);
    QCOMPARE(ApiPresets::capture().size(), Keys::apiPresetFields().size());
    config->setValue(Keys::apiExtraBody, QStringLiteral("{\"top_p\":0.1}"));
    QCOMPARE(ApiPresets::matchValues(presets, ApiPresets::capture()), -1);
}

void TestCore::apiPresetApplyWritesEveryField()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());
    ConfigManager* config = ConfigManager::instance();

    // Start from settings that differ from the preset in every way.
    config->setValue(Keys::apiBaseUrl, QStringLiteral("http://example.invalid"));
    config->setValue(Keys::apiKey, QStringLiteral("stale"));
    config->setValue(Keys::apiModel, QStringLiteral("stale-model"));
    config->setValue(Keys::apiMaxTokens, 1);
    config->setValue(Keys::apiTemperature, 1.5);
    config->setValue(Keys::apiStream, false);
    config->setValue(Keys::apiExtraBody, QStringLiteral("{\"x\":1}"));
    config->setValue(Keys::apiCustomHeaders, QStringLiteral("X-Stale: 1"));

    ApiPreset preset;
    preset.name = QStringLiteral("DeepSeek");
    preset.values.insert(Keys::apiBaseUrl, QStringLiteral("https://api.deepseek.com/v1"));
    preset.values.insert(Keys::apiKey, QStringLiteral("sk-deepseek"));
    preset.values.insert(Keys::apiModel, QStringLiteral("deepseek-chat"));
    preset.values.insert(Keys::apiMaxTokens, 8192);
    preset.values.insert(Keys::apiTemperature, 0.3);
    preset.values.insert(Keys::apiStream, true);
    preset.values.insert(Keys::apiExtraBody, QStringLiteral("{\"top_p\":0.9}"));
    preset.values.insert(Keys::apiCustomHeaders, QStringLiteral("X-Custom: yes"));

    ApiPresets::apply(preset);

    QCOMPARE(config->stringValue(Keys::apiBaseUrl), QStringLiteral("https://api.deepseek.com/v1"));
    QCOMPARE(config->stringValue(Keys::apiKey), QStringLiteral("sk-deepseek"));
    QCOMPARE(config->stringValue(Keys::apiModel), QStringLiteral("deepseek-chat"));
    QCOMPARE(config->intValue(Keys::apiMaxTokens), 8192);
    QCOMPARE(config->doubleValue(Keys::apiTemperature), 0.3);
    QCOMPARE(config->boolValue(Keys::apiStream), true);
    QCOMPARE(config->stringValue(Keys::apiExtraBody), QStringLiteral("{\"top_p\":0.9}"));
    QCOMPARE(config->stringValue(Keys::apiCustomHeaders), QStringLiteral("X-Custom: yes"));

    // The applied settings now match the preset that produced them.
    QCOMPARE(ApiPresets::matchValues({preset},
                                     [&]() {
                                         QJsonObject values;
                                         for (const QString& key : Keys::apiPresetFields())
                                             values.insert(key, config->value(key));
                                         return values;
                                     }()),
             0);

    // Fields a preset omits return to their defaults instead of keeping the
    // previous provider's values.
    ApiPreset sparse;
    sparse.name = QStringLiteral("Sparse");
    sparse.values.insert(Keys::apiBaseUrl, QStringLiteral("https://sparse.example/v1"));
    ApiPresets::apply(sparse);
    QCOMPARE(config->stringValue(Keys::apiBaseUrl), QStringLiteral("https://sparse.example/v1"));
    QVERIFY(config->isDefault(Keys::apiModel));
    QVERIFY(config->isDefault(Keys::apiKey));
    QVERIFY(config->isDefault(Keys::apiMaxTokens));
    QVERIFY(config->isDefault(Keys::apiTemperature));
    QVERIFY(config->isDefault(Keys::apiStream));
    QVERIFY(config->isDefault(Keys::apiCustomHeaders));

    // Presets survive a save/load cycle through the configuration store.
    config->setValue(Keys::apiPresets, ApiPresets::toJson({preset}));
    config->flush();
    const QVector<ApiPreset> reloaded =
        ApiPresets::fromJson(config->value(Keys::apiPresets).toArray());
    QCOMPARE(reloaded.size(), 1);
    QCOMPARE(reloaded.first().name, QStringLiteral("DeepSeek"));
    QCOMPARE(reloaded.first().values.value(Keys::apiKey).toString(), QStringLiteral("sk-deepseek"));
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

// A model that answers without a single usable group gets exactly one more
// chance, and a reply that does carry groups is never retried.
void TestCore::candidateRequestRetriesEmptyReply()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());

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
    context.uiLanguage = QStringLiteral("en");

    QVector<TranslationEngine::CandidateGroup> received;
    QString error;
    const QString word = QStringLiteral("皇帝");
    const int start = context.translatedText.indexOf(word);
    engine.requestCandidates(
        context, start, start + word.size(),
        [&](const QVector<TranslationEngine::CandidateGroup>& groups) { received = groups; },
        [&](const QString& message) { error = message; });

    QTRY_COMPARE_WITH_TIMEOUT(received.size(), 1, 5000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    // The unusable first reply was retried, and the second one was taken as final.
    QCOMPARE(requests, 2);
    QCOMPARE(received.first().target, QStringLiteral("皇帝"));
    QCOMPARE(optionTexts(received.first()), QStringList({QStringLiteral("君主")}));
    // The engine hands back the resolved span, so the caller never re-searches.
    QCOMPARE(received.first().start, start);
    QCOMPARE(received.first().length, word.size());
    QVERIFY(received.first().valid());
}

// The candidate request must open with the configured system prompt.
void TestCore::candidateRequestCarriesSystemPrompt()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    ConfigManager::instance()->setValue(
        Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()));
    ConfigManager::instance()->setValue(Keys::promptSystemEn,
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
    context.uiLanguage = QStringLiteral("en");

    QVector<TranslationEngine::CandidateGroup> received;
    const QString word = QStringLiteral("皇帝");
    const int start = context.translatedText.indexOf(word);
    engine.requestCandidates(
        context, start, start + word.size(),
        [&](const QVector<TranslationEngine::CandidateGroup>& groups) { received = groups; },
        [](const QString&) {});

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
void TestCore::candidateRequestPicksShortTextTemplate()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());

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
            prompts << requestUserPrompt(request.mid(headerEnd + 4, contentLength));
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
    context.uiLanguage = QStringLiteral("en");
    const QString word = QStringLiteral("皇帝");

    const QString shortText = QStringLiteral("莫卧儿皇帝是从什么时候开始觉得自己是印度人的？");
    QVector<TranslationEngine::CandidateGroup> received;
    const auto request = [&](const QString& translatedText, int start) {
        context.translatedText = translatedText;
        received.clear();
        engine.requestCandidates(
            context, start, start + word.size(),
            [&](const QVector<TranslationEngine::CandidateGroup>& groups) { received = groups; },
            [](const QString&) {});
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
