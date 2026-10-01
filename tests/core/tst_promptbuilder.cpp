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
    void buildsDocumentPrompt();
    void rendersWindowAsNumberedJson();
    void offersEveryPlaceholderToEveryTemplate();
    void listsEveryKnownPlaceholder();
};

namespace {

// Window as the document prompt carries it: one numbered entry per line, in
// line order, indented like the glossary block.
const QString kWindowJson = QStringLiteral("{\n"
                                           "    \"1\": \"第一行\",\n"
                                           "    \"2\": \"第二行\"\n"
                                           "}");

} // namespace

void TestPromptBuilder::substitutesContextValues()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    TranslationContext context;
    context.sourceText = QStringLiteral("Hello");
    context.targetLang = QStringLiteral("zh");

    PromptBuilder::Result result = PromptBuilder::build(context);
    QCOMPARE(result.system, Defaults::promptSystem);
    QVERIFY(result.user.contains(QStringLiteral("Chinese")));
    QVERIFY(result.user.contains(QStringLiteral("Hello")));
    QVERIFY(!result.user.contains(QStringLiteral("{target_lang}")));
    QVERIFY(!result.user.contains(QStringLiteral("{source_text}")));

    // A configured template overrides the built-in default system prompt.
    ConfigManager::instance()->setValue(Keys::promptSystem,
                                        QStringLiteral("You are an expert translator."));
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

    // No option set: the reference block is omitted entirely, so unset items
    // cannot pollute the prompt.
    QString prompt = PromptBuilder::build(context).user;
    QVERIFY(!prompt.contains(Defaults::promptReference.trimmed()));
    QVERIFY(!prompt.contains(QStringLiteral("- Translation tone:")));
    QVERIFY(!prompt.contains(QStringLiteral("- Language style:")));
    QVERIFY(!prompt.contains(QStringLiteral("- Background Information:")));
    QVERIFY(!prompt.contains(QStringLiteral("- Glossary:")));

    // The neutral tone is submitted like any other; the default tone and an
    // unset one both leave the entry out.
    context.tone = QStringLiteral("neutral");
    prompt = PromptBuilder::build(context).user;
    QVERIFY(prompt.startsWith(Defaults::promptReference.trimmed()));
    QVERIFY(prompt.contains(QStringLiteral("- Translation tone: neutral")));

    context.tone = QStringLiteral("default");
    prompt = PromptBuilder::build(context).user;
    QVERIFY(!prompt.contains(Defaults::promptReference.trimmed()));
    QVERIFY(!prompt.contains(QStringLiteral("- Translation tone:")));

    // Each set option appends exactly its own entry, in reference order.
    context.tone = QStringLiteral("formal");
    context.style = QStringLiteral("concise");
    context.background = QStringLiteral("deployment notes");
    prompt = PromptBuilder::build(context).user;
    QVERIFY(prompt.startsWith(Defaults::promptReference.trimmed()));
    QVERIFY(prompt.contains(QStringLiteral("formal")));
    QVERIFY(prompt.contains(QStringLiteral("concise")));
    QVERIFY(prompt.contains(QStringLiteral("deployment notes")));
    const int toneAt = prompt.indexOf(QStringLiteral("- Translation tone:"));
    const int styleAt = prompt.indexOf(QStringLiteral("- Language style:"));
    const int backgroundAt = prompt.indexOf(QStringLiteral("- Background Information:"));
    QVERIFY(toneAt >= 0 && styleAt > toneAt && backgroundAt > styleAt);

    // The instruction follows the reference block.
    QVERIFY(prompt.indexOf(QStringLiteral("Hello")) > backgroundAt);

    // A disabled or empty glossary contributes no entry.
    TranslationContext glossaryContext;
    glossaryContext.sourceText = QStringLiteral("Hello");
    glossaryContext.targetLang = QStringLiteral("en");
    glossaryContext.glossary = {{QStringLiteral("苹果"), QStringLiteral("Apple")}};
    glossaryContext.glossaryEnabled = false;
    QVERIFY(!PromptBuilder::build(glossaryContext).user.contains(QStringLiteral("- Glossary:")));
    glossaryContext.glossaryEnabled = true;
    QVERIFY(PromptBuilder::build(glossaryContext).user.contains(QStringLiteral("- Glossary:")));
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
    context.tone = QStringLiteral("default");
    QVERIFY(!PromptBuilder::build(context).user.contains(QStringLiteral("- Translation tone:")));

    context.tone = QStringLiteral("neutral");
    QVERIFY(PromptBuilder::build(context).user.contains(QStringLiteral("- Translation tone: neutral")));

    // The persisted default is the one the tone list leads with.
    QCOMPARE(Defaults::translationTone, Tones::presets().first().key);
    QVERIFY(Tones::labelFor(Defaults::translationTone) != nullptr);
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
    PromptBuilder::Result result = PromptBuilder::build(context);
    QVERIFY(result.user.contains(QStringLiteral("- Glossary:")));
    QVERIFY(result.user.contains(QStringLiteral("```json")));
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
    context.tone = QStringLiteral("formal");
    context.style = QStringLiteral("concise\ntechnical");
    context.background = QStringLiteral("line one\nline two");
    context.glossaryEnabled = true;
    context.glossary = {{QStringLiteral("fox"), QStringLiteral("Fuchs")}};

    const QString prompt = PromptBuilder::build(context).user;
    QVERIFY(prompt.contains(QStringLiteral("- Translation tone: formal")));
    // The style placeholder sits on the label line, so a multi-line value keeps
    // its line breaks without taking on indentation of its own.
    QVERIFY(prompt.contains(QStringLiteral("- Language style: concise\ntechnical")));
    QVERIFY(prompt.contains(QStringLiteral("- Background Information:\n  ```\n  line one\n  line two\n  ```")));
    QVERIFY(prompt.contains(QStringLiteral("- Glossary:\n  ```json\n  [\n      {\n          \"source\": \"fox\",")));
    QCOMPARE(prompt.count(Defaults::promptReference.trimmed()), 1);
    QVERIFY(prompt.indexOf(QStringLiteral("Hello")) > prompt.indexOf(Defaults::promptReference.trimmed()));

    // A single-line value stays on the label line.
    TranslationContext single = context;
    single.style = QStringLiteral("concise");
    QVERIFY(PromptBuilder::build(single).user.contains(QStringLiteral("- Language style: concise\n")));

    // An unset option removes its whole entry, indentation included.
    single.style.clear();
    QVERIFY(!PromptBuilder::build(single).user.contains(QStringLiteral("- Language style:")));
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

    const QString prompt = PromptBuilder::candidatePrompt(Keys::promptCandidate, context);
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

    const QString prompt =
        PromptBuilder::candidatePrompt(Keys::promptCandidateShort, context);

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
    ConfigManager::instance()->setValue(Keys::promptCandidateShort,
                                        QStringLiteral("Short text: {source_text}"));
    QCOMPARE(PromptBuilder::candidatePrompt(Keys::promptCandidateShort, context),
             QStringLiteral("Short text: Hello, world!"));
}

// A long document uses its own template, sharing the system prompt and the
// reference block with a translation request.

void TestPromptBuilder::buildsDocumentPrompt()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    DocumentWindowPrompt window;
    window.lines = {QStringLiteral("第一行"), QStringLiteral("第二行")};
    window.previous = QStringLiteral("前一段");
    window.next = QStringLiteral("后一段");

    TranslationContext context;
    context.targetLang = QStringLiteral("en");
    context.tone = QStringLiteral("formal");
    const PromptBuilder::Result result = PromptBuilder::buildDocument(context, window);
    QCOMPARE(result.system, Defaults::promptSystem);

    // The reference block leads the message exactly as it does for a
    // translation, so an unset option stays out of the document prompt too.
    QVERIFY(result.user.startsWith(Defaults::promptReference.trimmed()));
    QVERIFY(result.user.contains(QStringLiteral("- Translation tone: formal")));
    QVERIFY(!result.user.contains(QStringLiteral("- Language style:")));
    QVERIFY(result.user.indexOf(QStringLiteral("第一行"))
            > result.user.indexOf(QStringLiteral("- Translation tone:")));

    // The window reaches the model as a JSON object keyed by line number.
    const QJsonObject sent = TestSupport::documentWindowIn(result.user);
    QCOMPARE(sent.keys(), QStringList({QStringLiteral("1"), QStringLiteral("2")}));
    QCOMPARE(sent.value(QStringLiteral("1")).toString(), QStringLiteral("第一行"));
    QCOMPARE(sent.value(QStringLiteral("2")).toString(), QStringLiteral("第二行"));

    // Neighbouring windows and the target language are all injected.
    QVERIFY(result.user.contains(window.previous));
    QVERIFY(result.user.contains(window.next));
    QVERIFY(result.user.contains(QStringLiteral("English")));
    for (const QString& token : {QStringLiteral("{window}"), QStringLiteral("{window_lines}"),
                                 QStringLiteral("{prev_segment}"), QStringLiteral("{next_segment}"),
                                 QStringLiteral("{target_lang}")}) {
        QVERIFY2(!result.user.contains(token), qPrintable(result.user));
    }

    TranslationContext bare;
    bare.targetLang = QStringLiteral("en");
    const QString plain = PromptBuilder::buildDocument(bare, window).user;
    QVERIFY(!plain.contains(Defaults::promptReference.trimmed()));
    QVERIFY(!plain.contains(QStringLiteral("- Translation tone:")));

    // A document edge says so rather than leaving the context block empty.
    DocumentWindowPrompt edge;
    edge.lines = {QStringLiteral("only line")};
    const QString edgePrompt = PromptBuilder::buildDocument(bare, edge).user;
    QCOMPARE(edgePrompt.count(QStringLiteral("None")), 2);

    // The line count stays a number and the neighbours stay plain text, whatever
    // template carries them.
    ConfigManager::instance()->setValue(Keys::promptDocument,
                                        QStringLiteral("Lines: {window_lines}"));
    QCOMPARE(PromptBuilder::buildDocument(bare, window).user, QStringLiteral("Lines: 2"));

    ConfigManager::instance()->setValue(Keys::promptDocument,
                                        QStringLiteral("{prev_segment}\n{next_segment}"));
    QCOMPARE(PromptBuilder::buildDocument(bare, window).user,
             QStringLiteral("前一段\n后一段"));

    // A configured template overrides the built-in one, like every other prompt.
    ConfigManager::instance()->setValue(Keys::promptDocument,
                                        QStringLiteral("Lines: {window_lines}\n{window}"));
    QCOMPARE(PromptBuilder::buildDocument(bare, window).user,
             QStringLiteral("Lines: 2\n") + kWindowJson);
}

void TestPromptBuilder::rendersWindowAsNumberedJson()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    const QStringList lines = {QStringLiteral("first"), QStringLiteral("second"),
                               QStringLiteral("third")};
    const QString data = PromptBuilder::documentWindowData(lines);

    const QJsonDocument document = QJsonDocument::fromJson(data.toUtf8());
    QVERIFY2(document.isObject(), qPrintable(data));
    QCOMPARE(document.object().keys(),
             QStringList({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")}));
    for (int index = 0; index < lines.size(); ++index) {
        QCOMPARE(document.object().value(QString::number(index + 1)).toString(),
                 lines.at(index));
    }

    // One entry per line, keys in line order, each entry on its own indented line.
    const QStringList rendered = data.split(QLatin1Char('\n'));
    QCOMPARE(rendered.size(), lines.size() + 2);
    QCOMPARE(rendered.first(), QStringLiteral("{"));
    QCOMPARE(rendered.last(), QStringLiteral("}"));
    for (int index = 0; index < lines.size(); ++index) {
        const QString entry = rendered.at(index + 1);
        QVERIFY2(entry.startsWith(QStringLiteral("    \"")), qPrintable(entry));
        QCOMPARE(entry.section(QLatin1Char('"'), 1, 1), QString::number(index + 1));
    }

    // Numeric key order holds past nine lines, where sorting the keys as text
    // would put "10" before "2".
    QStringList many;
    for (int index = 0; index < 12; ++index)
        many << QStringLiteral("line %1").arg(index);
    const QStringList wide = PromptBuilder::documentWindowData(many).split(QLatin1Char('\n'));
    QCOMPARE(wide.size(), many.size() + 2);
    for (int index = 0; index < many.size(); ++index)
        QCOMPARE(wide.at(index + 1).section(QLatin1Char('"'), 1, 1), QString::number(index + 1));

    // Text needing escapes still round-trips through the object.
    const QStringList quoted = {QStringLiteral("He said \"hi\""), QStringLiteral("C:\\path")};
    const QJsonObject roundTrip =
        QJsonDocument::fromJson(PromptBuilder::documentWindowData(quoted).toUtf8()).object();
    QCOMPARE(roundTrip.value(QStringLiteral("1")).toString(), quoted.at(0));
    QCOMPARE(roundTrip.value(QStringLiteral("2")).toString(), quoted.at(1));
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
        Keys::promptSystem, Keys::promptReference, Keys::promptTone,
        Keys::promptStyle, Keys::promptBackground, Keys::promptGlossary,
        Keys::promptDefault, Keys::promptDocument, Keys::promptCandidate,
        Keys::promptCandidateShort
    };
    for (const QString& key : templates) {
        ConfigManager::instance()->setValue(key, probe);
        const QString rendered = PromptBuilder::candidatePrompt(key, context);
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
    const QString rendered = PromptBuilder::candidatePrompt(Keys::promptCandidate, bare);
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
        QStringLiteral("mark_left"), QStringLiteral("mark_right"),
        QStringLiteral("window"), QStringLiteral("window_lines"),
        QStringLiteral("prev_segment"), QStringLiteral("next_segment")
    };
    QCOMPARE(placeholders, expected);

    for (const QString& name : placeholders)
        QVERIFY(PromptBuilder::substitute(QStringLiteral("{%1}").arg(name),
                                          {{name, QStringLiteral("x")}}) == QStringLiteral("x"));
}

QTEST_MAIN(TestPromptBuilder)
#include "tst_promptbuilder.moc"
