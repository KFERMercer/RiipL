#include <QtTest>

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/history/HistoryManager.h"
#include "core/models/Glossary.h"
#include "core/translation/PromptBuilder.h"
#include "core/translation/TranslationEngine.h"
#include "utils/TextUtils.h"
#include "ui/widgets/TranslationEdit.h"

#include <QJsonDocument>
#include <QTemporaryDir>

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    void configFallsBackToDefaults();
    void configStoresOnlyNonDefaults();
    void configPersistsAcrossInstances();
    void promptSubstitution();
    void promptGlossaryFormatting();
    void candidatePromptSubstitution();
    void glossaryRoundTrip();
    void historyTrimming();
    void uiLanguageResolution();
    void candidateCleaning();
    void wordSpanAtBoundaries();
    void candidateResponseParsing();
    void replaceTargetsCompleteWord();

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

void TestCore::promptSubstitution()
{
    QDir().mkpath(tempDir());
    ConfigManager::createInstance(tempDir());

    TranslationContext context;
    context.sourceText = QStringLiteral("Hello");
    context.targetLang = QStringLiteral("zh");
    context.uiLanguage = QStringLiteral("en");
    const PromptBuilder::Result result = PromptBuilder::build(context);
    QVERIFY(result.system.isEmpty());
    QVERIFY(result.user.contains(QStringLiteral("Chinese")));
    QVERIFY(result.user.contains(QStringLiteral("Hello")));
    QVERIFY(!result.user.contains(QStringLiteral("{target_lang}")));
    QVERIFY(!result.user.contains(QStringLiteral("{source_text}")));
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
    QVERIFY(!segment.contains(QLatin1Char('\uff0c')));
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
    TranslationEdit edit;
    const QString translated = QStringLiteral("莫卧儿皇帝是从什么时候开始觉得自己是印度人的？");
    edit.setResult(translated);

    const int huangIndex = translated.indexOf(QStringLiteral("皇"));
    const TextUtils::WordSpan span = TextUtils::wordSpanAt(translated, huangIndex);
    QVERIFY(span.valid());

    QTextCursor hint = edit.textCursor();
    hint.setPosition(span.start);
    hint.setPosition(span.end, QTextCursor::KeepAnchor);

    QVERIFY(edit.replaceWordAt(hint, QStringLiteral("皇帝"), QStringLiteral("君主")));
    QCOMPARE(edit.toPlainText(),
             QStringLiteral("莫卧儿君主是从什么时候开始觉得自己是印度人的？"));
}

QTEST_MAIN(TestCore)
#include "test_core.moc"
