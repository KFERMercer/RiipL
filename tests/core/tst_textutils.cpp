#include <QtTest>

#include "utils/TextUtils.h"

class TestTextUtils : public QObject
{
    Q_OBJECT

private slots:
    void findsWordsAtBoundaries();
    void windowsCandidateFragments();
    void countsWordsInFragmentWindow();
    void countsSupplementaryIdeographs();
};

void TestTextUtils::findsWordsAtBoundaries()
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

void TestTextUtils::windowsCandidateFragments()
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

void TestTextUtils::countsWordsInFragmentWindow()
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

void TestTextUtils::countsSupplementaryIdeographs()
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
    // rejected for length exactly as nine Hangul syllables are.
    QCOMPARE(TextUtils::wordSpanAt(QStringLiteral("\uA960").repeated(8), 0).valid(), true);
    QCOMPARE(TextUtils::wordSpanAt(QStringLiteral("\uA960").repeated(9), 0).valid(), false);
    QCOMPARE(TextUtils::wordSpanAt(QStringLiteral("\u11A8").repeated(9), 0).valid(), false);
}

QTEST_MAIN(TestTextUtils)
#include "tst_textutils.moc"
