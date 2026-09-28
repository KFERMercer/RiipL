#include <QtTest>

#include "TestSupport.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/translation/PromptBuilder.h"
#include "core/translation/Tone.h"

class TestPromptBuilder : public QObject
{
    Q_OBJECT

private slots:
    void substitutesContextValues();
    void omitsUnsetReferenceEntries();
    void writesToneEntries();
    void formatsGlossaryAsJson();
    void indentsMultiLineEntries();
    void marksSelectionInCandidatePrompt();
    void rendersShortTextTemplate();
    void offersEveryPlaceholderToEveryTemplate();
    void listsEveryKnownPlaceholder();
};

void TestPromptBuilder::substitutesContextValues()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

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

void TestPromptBuilder::omitsUnsetReferenceEntries()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    TranslationContext context;
    context.sourceText = QStringLiteral("Hello");
    context.targetLang = QStringLiteral("zh");
    context.uiLanguage = QStringLiteral("zh");

    // No option set: the reference block is omitted entirely, so unset items
    // cannot pollute the prompt.
    QString prompt = PromptBuilder::build(context).user;
    QVERIFY(!prompt.contains(Defaults::promptReferenceZh.trimmed()));
    QVERIFY(!prompt.contains(QStringLiteral("- 翻译语气：")));
    QVERIFY(!prompt.contains(QStringLiteral("- 语言风格：")));
    QVERIFY(!prompt.contains(QStringLiteral("- 背景信息：")));
    QVERIFY(!prompt.contains(QStringLiteral("- 术语表：")));

    // The neutral tone is submitted like any other; the default tone and an
    // unset one both leave the entry out.
    context.tone = QStringLiteral("neutral");
    prompt = PromptBuilder::build(context).user;
    QVERIFY(prompt.startsWith(Defaults::promptReferenceZh.trimmed()));
    QVERIFY(prompt.contains(QStringLiteral("- 翻译语气：neutral")));

    context.tone = QStringLiteral("default");
    prompt = PromptBuilder::build(context).user;
    QVERIFY(!prompt.contains(Defaults::promptReferenceZh.trimmed()));
    QVERIFY(!prompt.contains(QStringLiteral("- 翻译语气：")));

    // Each set option appends exactly its own entry, in reference order.
    context.tone = QStringLiteral("formal");
    context.style = QStringLiteral("concise");
    context.background = QStringLiteral("deployment notes");
    prompt = PromptBuilder::build(context).user;
    QVERIFY(prompt.startsWith(Defaults::promptReferenceZh.trimmed()));
    QVERIFY(prompt.contains(QStringLiteral("formal")));
    QVERIFY(prompt.contains(QStringLiteral("concise")));
    QVERIFY(prompt.contains(QStringLiteral("deployment notes")));
    const int toneAt = prompt.indexOf(QStringLiteral("- 翻译语气："));
    const int styleAt = prompt.indexOf(QStringLiteral("- 语言风格："));
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

// The default tone is the absence of a tone: it stays out of the prompt like an
// unset one, while every other preset reaches the model by its key.

void TestPromptBuilder::writesToneEntries()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    TranslationContext context;
    context.sourceText = QStringLiteral("Hello");
    context.targetLang = QStringLiteral("de");
    context.uiLanguage = QStringLiteral("zh");
    context.tone = QStringLiteral("default");
    QVERIFY(!PromptBuilder::build(context).user.contains(QStringLiteral("- 翻译语气：")));

    context.tone = QStringLiteral("neutral");
    QVERIFY(PromptBuilder::build(context).user.contains(QStringLiteral("- 翻译语气：neutral")));

    // The persisted default is the one the tone list leads with.
    QCOMPARE(Defaults::translationTone, Tones::presets().first().key);
    QVERIFY(!Tones::presetDisplayName(Defaults::translationTone, QStringLiteral("zh")).isEmpty());
}

void TestPromptBuilder::formatsGlossaryAsJson()
{
    QVector<GlossaryEntry> entries;
    entries.append({QStringLiteral("苹果"), QStringLiteral("Apple")});
    entries.append({QStringLiteral("RiipL"), QString()});

    const QString data = PromptBuilder::glossaryData(entries);
    const QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    QVERIFY(doc.isArray());
    const QJsonArray array = doc.array();
    QCOMPARE(array.size(), 2);
    QCOMPARE(array.at(0).toObject().value(QStringLiteral("source")).toString(), QStringLiteral("苹果"));
    QCOMPARE(array.at(0).toObject().value(QStringLiteral("target")).toString(), QStringLiteral("Apple"));
    QCOMPARE(array.at(1).toObject().value(QStringLiteral("source")).toString(), QStringLiteral("RiipL"));
    // A term without a target is mapped to itself, which states "keep as-is"
    // without a null literal that some models echo verbatim.
    QCOMPARE(array.at(1).toObject().value(QStringLiteral("target")).toString(), QStringLiteral("RiipL"));

    // Entries without a source term carry no information and are dropped.
    QVERIFY(PromptBuilder::glossaryData({{QString(), QStringLiteral("Apple")}}).isEmpty());

    TranslationContext context;
    context.sourceText = QStringLiteral("苹果 is good");
    context.targetLang = QStringLiteral("en");
    context.glossary = entries;
    context.uiLanguage = QStringLiteral("zh");
    PromptBuilder::Result result = PromptBuilder::build(context);
    QVERIFY(result.user.contains(QStringLiteral("- 术语表：")));
    QVERIFY(result.user.contains(QStringLiteral("```json")));
    QVERIFY(result.user.contains(QStringLiteral("\"source\": \"苹果\"")));

    // The keys do not follow the UI language, so the same entries render the
    // same way whichever template language labels them.
    context.uiLanguage = QStringLiteral("en");
    result = PromptBuilder::build(context);
    QVERIFY(result.user.contains(QStringLiteral("\"source\": \"苹果\"")));
    QVERIFY(result.user.contains(QStringLiteral("\"target\": \"Apple\"")));
}

void TestPromptBuilder::indentsMultiLineEntries()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

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
    QVERIFY(prompt.contains(QStringLiteral("- 翻译语气：formal")));
    QVERIFY(prompt.contains(QStringLiteral("- 语言风格：\n  ```\n  concise\n  technical\n  ```")));
    QVERIFY(prompt.contains(QStringLiteral("- 背景信息：\n  ```\n  line one\n  line two\n  ```")));
    QVERIFY(prompt.contains(QStringLiteral("- 术语表：\n  ```json\n  [\n      {\n          \"source\": \"fox\",")));
    QCOMPARE(prompt.count(Defaults::promptReferenceZh.trimmed()), 1);
    QVERIFY(prompt.indexOf(QStringLiteral("Hello")) > prompt.indexOf(Defaults::promptReferenceZh.trimmed()));

    // A single-line value keeps the template's own indentation untouched.
    TranslationContext single = context;
    single.style = QStringLiteral("concise");
    QVERIFY(PromptBuilder::build(single).user.contains(QStringLiteral("  concise")));

    // An unset option removes its whole entry, indentation included.
    single.style.clear();
    QVERIFY(!PromptBuilder::build(single).user.contains(QStringLiteral("- 语言风格：")));
}

void TestPromptBuilder::marksSelectionInCandidatePrompt()
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

void TestPromptBuilder::rendersShortTextTemplate()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

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

void TestPromptBuilder::offersEveryPlaceholderToEveryTemplate()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

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

void TestPromptBuilder::listsEveryKnownPlaceholder()
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

QTEST_MAIN(TestPromptBuilder)
#include "tst_promptbuilder.moc"
