#include <QSet>
#include <QtTest>

#include "core/translation/Language.h"

class TestLanguages : public QObject
{
    Q_OBJECT

private slots:
    void guessesLanguageFromScript();
    void detectsSupplementaryPlanes();
    void ignoresScriptlessAttachments();
    void breaksTiesDeterministically();
    void resolveAutoExcludesTarget();
    void exposesTranslatableNames();
    void parsesCustomLanguages();
    void roundTripsCustomLanguages();
    void resolvesBuiltInAndCustomKeys();
    void exposesBcp47Tags();
};

void TestLanguages::guessesLanguageFromScript()
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

void TestLanguages::detectsSupplementaryPlanes()
{
    using namespace Languages;
    // Supplementary-plane ideographs are classified through their surrogate
    // pair rather than by inspecting the packed UTF-16 code unit.
    QCOMPARE(guessFromScript(QString::fromUcs4(U"\U00020000", 1)), QStringLiteral("zh"));
    QCOMPARE(guessFromScript(QString::fromUcs4(U"\U0002A6DF", 1)), QStringLiteral("zh"));
    QCOMPARE(guessFromScript(QString::fromUcs4(U"\U00020000\U00020001", 2)), QStringLiteral("zh"));
    // Hangul Jamo Extended-A carries the Hangul script but sits outside the
    // Hangul syllables block.
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

void TestLanguages::ignoresScriptlessAttachments()
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

void TestLanguages::breaksTiesDeterministically()
{
    using namespace Languages;
    // Hiragana and katakana count towards one language, so a text holding as many
    // Han characters as kana resolves to Japanese rather than Chinese.
    QCOMPARE(guessFromScript(QStringLiteral("あ漢")), QStringLiteral("ja"));
    QCOMPARE(guessFromScript(QStringLiteral("ア漢")), QStringLiteral("ja"));
    QCOMPARE(guessFromScript(QStringLiteral("あア漢")), QStringLiteral("ja"));
    // When two languages tie, the one listed first wins.
    QCOMPARE(guessFromScript(QStringLiteral("אב Пр")), QStringLiteral("he"));
    QCOMPARE(guessFromScript(QStringLiteral("Пр אב")), QStringLiteral("he"));
}

void TestLanguages::resolveAutoExcludesTarget()
{
    using namespace Languages;
    // An auto-detected language that equals the target falls back to another
    // one, so a request never asks for a translation into its own language.
    QCOMPARE(resolveAuto(QStringLiteral("你好"), QStringLiteral("en")), QStringLiteral("zh"));
    QCOMPARE(resolveAuto(QStringLiteral("你好"), QStringLiteral("fr")), QStringLiteral("zh"));
    const QString latinFallback = resolveAuto(QStringLiteral("Hello world"), QStringLiteral("en"));
    QVERIFY(latinFallback != QStringLiteral("en"));
    QVERIFY(indexOf(latinFallback) > 0);
    QVERIFY(resolveAuto(QStringLiteral("你好"), QStringLiteral("zh")) != QStringLiteral("zh"));
    QVERIFY(resolveAuto(QString(), QStringLiteral("ja")) != QStringLiteral("ja"));
}

void TestLanguages::exposesTranslatableNames()
{
    // The UI resolves these through the "Languages" catalog.
    for (const LangItem& lang : Languages::all())
        QVERIFY2(lang.en && *lang.en, qPrintable(lang.code));
    QVERIFY(Languages::labelFor(QStringLiteral("zh")) != nullptr);
    QVERIFY(Languages::labelFor(QStringLiteral("zh-Hant")) != nullptr);
    QVERIFY(Languages::labelFor(QStringLiteral("not-a-language")) == nullptr);

    // The prompt text is English regardless of locale.
    QCOMPARE(Languages::englishName(QStringLiteral("zh")), QStringLiteral("Chinese"));
    QCOMPARE(Languages::englishName(QStringLiteral("auto")), QStringLiteral("the detected language"));
    QCOMPARE(Languages::englishName(QStringLiteral("not-a-language")), QStringLiteral("not-a-language"));
}

void TestLanguages::parsesCustomLanguages()
{
    using namespace Languages;
    const QJsonArray stored = {
        QJsonObject{{QStringLiteral("key"), QStringLiteral("  Klingon  ")},
                    {QStringLiteral("name"), QStringLiteral("  Klingon (custom)  ")}},
        QJsonObject{{QStringLiteral("key"), QStringLiteral("x-nameless")},
                    {QStringLiteral("name"), QStringLiteral("   ")}},
        QJsonObject{{QStringLiteral("key"), QStringLiteral("   ")},
                    {QStringLiteral("name"), QStringLiteral("Dropped")}},
        QJsonObject{{QStringLiteral("name"), QStringLiteral("No key")}},
        QJsonValue(QStringLiteral("not-an-object")),
        QJsonObject{{QStringLiteral("key"), QStringLiteral("x-last")},
                    {QStringLiteral("name"), QStringLiteral("Last")}}
    };

    const QVector<CustomLanguage> parsed = customFromJson(stored);
    QCOMPARE(parsed.size(), 3);
    QCOMPARE(parsed.at(0).key, QStringLiteral("Klingon"));
    QCOMPARE(parsed.at(0).name, QStringLiteral("Klingon (custom)"));
    // An entry without a name is displayed under its key.
    QCOMPARE(parsed.at(1).key, QStringLiteral("x-nameless"));
    QCOMPARE(parsed.at(1).name, QStringLiteral("x-nameless"));
    QCOMPARE(parsed.at(2).key, QStringLiteral("x-last"));
    QCOMPARE(parsed.at(2).name, QStringLiteral("Last"));

    QVERIFY(customFromJson(QJsonArray()).isEmpty());

    // The shared normaliser, exercised without the JSON reader.
    const QVector<CustomLanguage> normalized = customNormalized({
        {QStringLiteral("  Klingon  "), QStringLiteral("  Klingon (custom)  ")},
        {QStringLiteral("x-nameless"), QString()},
        {QStringLiteral("  "), QStringLiteral("Dropped")}
    });
    QCOMPARE(normalized.size(), 2);
    QCOMPARE(normalized.at(0).key, QStringLiteral("Klingon"));
    QCOMPARE(normalized.at(0).name, QStringLiteral("Klingon (custom)"));
    QCOMPARE(normalized.at(1).key, QStringLiteral("x-nameless"));
    QCOMPARE(normalized.at(1).name, QStringLiteral("x-nameless"));
}

void TestLanguages::roundTripsCustomLanguages()
{
    using namespace Languages;
    const QVector<CustomLanguage> custom = {
        {QStringLiteral("Klingon"), QStringLiteral("Klingon (custom)")},
        {QStringLiteral("x-kl"), QStringLiteral("Klingon")}
    };

    const QJsonArray stored = customToJson(custom);
    QCOMPARE(stored.size(), 2);
    QCOMPARE(stored.at(0).toObject().keys(),
             QStringList({QStringLiteral("key"), QStringLiteral("name")}));
    QCOMPARE(stored.at(0).toObject().value(QStringLiteral("key")).toString(),
             QStringLiteral("Klingon"));
    QCOMPARE(stored.at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("Klingon (custom)"));

    const QVector<CustomLanguage> parsed = customFromJson(stored);
    QCOMPARE(parsed.size(), custom.size());
    for (qsizetype index = 0; index < custom.size(); ++index) {
        QCOMPARE(parsed.at(index).key, custom.at(index).key);
        QCOMPARE(parsed.at(index).name, custom.at(index).name);
    }

    QVERIFY(customToJson({}).isEmpty());
    QVERIFY(customFromJson(customToJson({})).isEmpty());
}

void TestLanguages::resolvesBuiltInAndCustomKeys()
{
    using namespace Languages;
    QCOMPARE(englishName(QStringLiteral("en")), QStringLiteral("English"));
    QCOMPARE(englishName(QStringLiteral("auto")), QStringLiteral("the detected language"));
    QCOMPARE(englishName(QStringLiteral("not-a-language")), QStringLiteral("not-a-language"));
    QCOMPARE(englishName(QString()), QString());

    const QVector<CustomLanguage> custom = customFromJson(
        QJsonArray{QJsonObject{{QStringLiteral("key"), QStringLiteral("Klingon")},
                               {QStringLiteral("name"), QStringLiteral("Klingon (custom)")}},
                   QJsonObject{{QStringLiteral("key"), QStringLiteral("en")},
                               {QStringLiteral("name"), QStringLiteral("Overridden English")}}});
    QCOMPARE(custom.size(), 2);

    QCOMPARE(englishName(custom.at(0).key), QStringLiteral("Klingon"));
    QVERIFY(englishName(custom.at(0).key) != custom.at(0).name);
    // A stored entry cannot rename a built-in code.
    QCOMPARE(englishName(custom.at(1).key), QStringLiteral("English"));
}

void TestLanguages::exposesBcp47Tags()
{
    QSet<QString> tags;
    for (const LangItem& lang : Languages::all()) {
        if (lang.code == QLatin1String("auto"))
            continue;
        const QStringList subtags = lang.code.split(QLatin1Char('-'));
        QCOMPARE(QLocale::languageToCode(QLocale::codeToLanguage(subtags.constFirst())),
                 subtags.constFirst());
        for (qsizetype i = 1; i < subtags.size(); ++i) {
            const QString& subtag = subtags.at(i);
            QCOMPARE(subtag.size() == 4
                         ? QLocale::scriptToCode(QLocale::codeToScript(subtag))
                         : QLocale::territoryToCode(QLocale::codeToTerritory(subtag)),
                     subtag);
        }
        QVERIFY2(!tags.contains(lang.code), qPrintable(lang.code));
        tags.insert(lang.code);
    }
}

QTEST_MAIN(TestLanguages)
#include "tst_languages.moc"
