#include <QtTest>

#include "TestSupport.h"
#include "core/translation/Language.h"

class TestLanguages : public QObject
{
    Q_OBJECT

private slots:
    void guessesLanguageFromScript();
    void detectsSupplementaryPlanes();
    void ignoresScriptlessAttachments();
    void resolveAutoExcludesTarget();
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

QTEST_MAIN(TestLanguages)
#include "tst_languages.moc"
