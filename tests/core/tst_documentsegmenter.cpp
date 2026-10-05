#include <QtTest>

#include "core/config/Defaults.h"
#include "core/document/DocumentSegmenter.h"
#include "utils/TextUtils.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <optional>

namespace {

// One translated line per window line, so assembling reproduces the document.
QVector<QStringList> sourceLines(const QVector<DocumentWindow>& windows)
{
    QVector<QStringList> translations;
    for (const DocumentWindow& window : windows) {
        QStringList lines;
        for (const DocumentLine& line : window.lines)
            lines << line.text;
        translations << lines;
    }
    return translations;
}

// Line the document holds once, with no run before it.
DocumentLine lineOf(const QString& text)
{
    DocumentLine line;
    line.text = text;
    line.whitespaceBefore.append(QString());
    return line;
}

// Window holding \p lines once each, with no run between them.
DocumentWindow windowOf(const QStringList& lines)
{
    DocumentWindow window;
    for (const QString& line : lines)
        window.lines.append(lineOf(line));
    return window;
}

// Line of \p count words, with no whitespace around it.
QString wordLine(const QString& word, int count)
{
    return QStringList(count, word).join(QLatin1Char(' '));
}

// Answer object keyed from 1 on, as the prompt asks the model to reply.
QString windowJson(const QStringList& values)
{
    QJsonObject answer;
    for (int index = 0; index < values.size(); ++index)
        answer.insert(QString::number(index + 1), values.at(index));
    return QString::fromUtf8(QJsonDocument(answer).toJson(QJsonDocument::Compact));
}

} // namespace

class TestDocumentSegmenter : public QObject
{
    Q_OBJECT

private slots:
    void partitionsIntoWholeLineWindows();
    void packsByWordsWhenLinesAllow();
    void fillsWindowsUpToTheDefaultWordLimit();
    void fitsExactlyTheWordLimit();
    void keepsOversizedLineInItsOwnWindow();
    void recordsBlankLinesAndRepeats();
    void foldsRepeatsAcrossBlankLines();
    void keepsBlanksAFoldDidNotBridge();
    void requestsLinesWithoutTheWhitespaceAroundThem();
    void keepsBlankLinesOfWhitespaceAsWritten();
    void foldsLinesThatDifferOnlyInIndent();
    void foldsLinesThatDifferOnlyInTrailingWhitespace();
    void restoresWhitespaceAcrossWindows();
    void restoresDocumentFromTranslations();
    void yieldsNoWindowForCaptionsAndBlankText();
    void endsWindowAtLineLimitBeforeWordLimit();
    void countsWordsAfterCollapsingRepeats();
    void rendersWindowWithRepeatsAndBlanks();
    void splitsWindowByLineNumber();
    void readsKeysInNumberOrder();
    void acceptsAnEchoWrappedInLines();
    void rejectsInvalidKeySets();
    void rejectsAnswersThatAreNotAWindow();
    void givesUpOnAnAnswerWithoutEntries();
    void decodesEscapedValues();
    void keepsNewlineInsideValue();
    void readsQuotesTheAnswerLeftUnescaped();
    void salvagesValuesTheAnswerLeftUnclosed();
    void mapsRepeatedLinesOntoFoldedLines();
    void stripsCodeFenceFromAnswer();
    void parsesAnswerOfAWindowOfFences();
    void splitsByConfiguredWindowBounds();
};

void TestDocumentSegmenter::partitionsIntoWholeLineWindows()
{
    QStringList lines;
    for (int index = 0; index < 25; ++index)
        lines << QStringLiteral("l%1 a b c d e f g h i j k").arg(index);
    const QString document = lines.join(QLatin1Char('\n'));

    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document);
    QCOMPARE(windows.size(), 3);
    QCOMPARE(windows.at(0).lineCount(), Defaults::documentWindowLines);
    QCOMPARE(windows.at(1).lineCount(), Defaults::documentWindowLines);
    QCOMPARE(windows.at(2).lineCount(), 5);
    QVERIFY(windows.at(0).source().startsWith(QStringLiteral("l0 a b")));

    // Whole lines only, and never more than a window may hold.
    int start = 0;
    for (const DocumentWindow& window : windows) {
        QVERIFY(window.lineCount() <= Defaults::documentWindowLines);
        QCOMPARE(window.source(), lines.mid(start, window.lineCount()).join(QLatin1Char('\n')));
        start += window.lineCount();
        QVERIFY(!window.source().isEmpty());
    }
    QCOMPARE(start, lines.size());
}

// With the lines to spare, the word limit is what closes a window.
void TestDocumentSegmenter::packsByWordsWhenLinesAllow()
{
    QStringList lines;
    for (int index = 0; index < 20; ++index) {
        lines << QString::number(index).rightJustified(3, QLatin1Char('0'))
                     + QStringLiteral(" abcdefghijklm");
    }
    const QString document = lines.join(QLatin1Char('\n'));

    // Two words per line, so a window of eight words holds four of them.
    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document, 8, 100);
    QCOMPARE(windows.size(), 5);
    for (const DocumentWindow& window : windows) {
        QCOMPARE(window.lineCount(), 4);
        QCOMPARE(TextUtils::wordCount(window.source()), 8);
    }

    // Every line reaches the model exactly once, and no window overshoots.
    QCOMPARE(DocumentSegmenter::assemble(windows, sourceLines(windows)), document);
}

// Lines whose words sum to the default limit fill a window; one word more opens
// the next.
void TestDocumentSegmenter::fillsWindowsUpToTheDefaultWordLimit()
{
    // A line of one word and a line of the rest of the limit fill it exactly.
    const QString words = wordLine(QStringLiteral("a"), Defaults::documentWindowWords - 1);
    const QVector<DocumentWindow> fitted =
        DocumentSegmenter::partition(QStringLiteral("1\n") + words);
    QCOMPARE(fitted.size(), 1);
    QCOMPARE(fitted.first().lineCount(), 2);
    QCOMPARE(TextUtils::wordCount(fitted.first().source()), Defaults::documentWindowWords);

    // One word more on the short line leaves the long one in a window of its own.
    const QVector<DocumentWindow> split =
        DocumentSegmenter::partition(QStringLiteral("1 1\n") + words);
    QCOMPARE(split.size(), 2);
    QCOMPARE(split.at(0).source(), QStringLiteral("1 1"));
    QCOMPARE(split.at(1).source(), words);

    // A line of one word stays far from the limit, so the line limit fills a
    // window of them.
    QStringList many;
    for (int index = 0; index < Defaults::documentWindowWords; ++index)
        many << QStringLiteral("x%1").arg(index);
    const QVector<DocumentWindow> capped =
        DocumentSegmenter::partition(many.join(QLatin1Char('\n')));
    QCOMPARE(capped.size(), Defaults::documentWindowWords / Defaults::documentWindowLines);
    for (const DocumentWindow& window : capped)
        QCOMPARE(window.lineCount(), Defaults::documentWindowLines);
    QCOMPARE(DocumentSegmenter::assemble(capped, sourceLines(capped)),
             many.join(QLatin1Char('\n')));
}

// A window fills up to the limit without crossing it.
void TestDocumentSegmenter::fitsExactlyTheWordLimit()
{
    const QString document = QStringLiteral("a b\nc d\ne f");

    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document, 4, 10);
    QCOMPARE(windows.size(), 2);
    QCOMPARE(windows.at(0).source(), QStringLiteral("a b\nc d"));
    QCOMPARE(windows.at(1).source(), QStringLiteral("e f"));

    // A single line whose own words are the limit closes alone.
    const QVector<DocumentWindow> single =
        DocumentSegmenter::partition(QStringLiteral("a b c"), 2, 10);
    QCOMPARE(single.size(), 1);
    QCOMPARE(single.first().lineCount(), 1);
}

// A line over the word limit fills a window on its own, and one over the line
// limit leaves with the lines that fitted before it.
void TestDocumentSegmenter::keepsOversizedLineInItsOwnWindow()
{
    const QString oversized = wordLine(QStringLiteral("a"), Defaults::documentWindowWords + 1);
    const QString document = QStringLiteral("a b c\n") + oversized + QStringLiteral("\na b c");

    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document);
    QCOMPARE(windows.size(), 3);
    QCOMPARE(windows.at(0).source(), QStringLiteral("a b c"));
    QCOMPARE(windows.at(1).lineCount(), 1);
    QCOMPARE(windows.at(1).source(), oversized);
    QCOMPARE(windows.at(2).source(), QStringLiteral("a b c"));

    const QString longLine = wordLine(QStringLiteral("z"), 150);
    const QString tenLines = QStringLiteral("a\nb\nc\nd\ne\nf\ng\nh\ni\n") + longLine;
    const QVector<DocumentWindow> capped = DocumentSegmenter::partition(tenLines, 100, 10);
    QCOMPARE(capped.size(), 2);
    QCOMPARE(capped.at(0).lineCount(), 9);
    QCOMPARE(capped.at(1).source(), longLine);

    // Two lines that together cross the limit do not share a window, even though
    // each of them fits it alone, while a short line leaves room for one that
    // crosses it.
    const QString first = wordLine(QStringLiteral("a"), 6);
    const QString second = wordLine(QStringLiteral("b"), 6);
    const QVector<DocumentWindow> pair =
        DocumentSegmenter::partition(first + QStringLiteral("\n") + second, 10, 10);
    QCOMPARE(pair.size(), 2);
    QCOMPARE(pair.at(0).source(), first);
    QCOMPARE(pair.at(1).source(), second);

    const QString shortLine = wordLine(QStringLiteral("s"), 4);
    const QVector<DocumentWindow> mixed =
        DocumentSegmenter::partition(shortLine + QStringLiteral("\n") + first, 10, 10);
    QCOMPARE(mixed.size(), 1);
    QCOMPARE(mixed.first().lineCount(), 2);
}

void TestDocumentSegmenter::recordsBlankLinesAndRepeats()
{
    const QString document = QStringLiteral("alpha one\n"
                                            "\n"
                                            "beta two\n"
                                            "beta two\n"
                                            "beta two\n"
                                            "\n"
                                            "\n"
                                            "gamma three\n");

    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document);
    QCOMPARE(windows.size(), 1);
    const DocumentWindow& window = windows.first();
    QCOMPARE(window.lineCount(), 3);
    QCOMPARE(window.lines.at(0).whitespaceBefore, QStringList({QString()}));
    QCOMPARE(window.lines.at(1).text, QStringLiteral("beta two"));
    QCOMPARE(window.lines.at(1).whitespaceBefore,
             QStringList({QStringLiteral("\n\n"), QStringLiteral("\n"),
                          QStringLiteral("\n")}));
    QCOMPARE(window.lines.at(2).whitespaceBefore, QStringList({QStringLiteral("\n\n\n")}));
    QCOMPARE(window.whitespaceAfter, QStringLiteral("\n"));

    // The collapsed document reaches the model once per line.
    QCOMPARE(window.source(), QStringLiteral("alpha one\nbeta two\ngamma three"));
    QCOMPARE(DocumentSegmenter::assemble(windows, {}), document);
}

// Blank lines carry no text, so they do not separate two equal lines: folding
// runs over them.
void TestDocumentSegmenter::foldsRepeatsAcrossBlankLines()
{
    const QString document = QStringLiteral("aaa\n"
                                            "bbb\n"
                                            "\n"
                                            "bbb\n"
                                            "\n"
                                            "bbb\n"
                                            "  \n"
                                            "\n"
                                            "bbb\n"
                                            "ccc\n");

    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document);
    QCOMPARE(windows.size(), 1);
    const DocumentWindow& window = windows.first();

    // The four occurrences reach the model as one line, each keeping its blank
    // lines, the line of spaces among them.
    QCOMPARE(window.lineCount(), 3);
    QCOMPARE(window.lines.at(0).text, QStringLiteral("aaa"));
    QCOMPARE(window.lines.at(1).text, QStringLiteral("bbb"));
    QCOMPARE(window.lines.at(1).whitespaceBefore,
             QStringList({QStringLiteral("\n"), QStringLiteral("\n\n"),
                          QStringLiteral("\n\n"), QStringLiteral("\n  \n\n")}));
    QCOMPARE(window.lines.at(2).text, QStringLiteral("ccc"));
    QCOMPARE(window.source(), QStringLiteral("aaa\nbbb\nccc"));

    // The answer lands where the document wrote the line.
    const QStringList translated = {QStringLiteral("甲"), QStringLiteral("乙"),
                                    QStringLiteral("丙")};
    const QString rendered = DocumentSegmenter::renderWindow(window, translated);
    QCOMPARE(rendered, QStringLiteral("甲\n乙\n\n乙\n\n乙\n  \n\n乙\n丙\n"));
    QCOMPARE(DocumentSegmenter::assemble(windows, {translated}), rendered);
    QCOMPARE(DocumentSegmenter::assemble(windows, {}), document);
}

// A document comes back from its windows as it was written, whatever blanks and
// folds it holds.
void TestDocumentSegmenter::keepsBlanksAFoldDidNotBridge()
{
    const QString document = QStringLiteral("aaa\n"
                                            "\n"
                                            "\n"
                                            "bbb\n"
                                            "ccc\n"
                                            "\n"
                                            "ccc\n"
                                            "ddd\n");
    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document);
    QCOMPARE(windows.size(), 1);
    const DocumentWindow& window = windows.first();

    QCOMPARE(window.lineCount(), 4);
    QCOMPARE(window.lines.at(1).text, QStringLiteral("bbb"));
    QCOMPARE(window.lines.at(1).whitespaceBefore, QStringList({QStringLiteral("\n\n\n")}));
    QCOMPARE(window.lines.at(2).whitespaceBefore,
             QStringList({QStringLiteral("\n"), QStringLiteral("\n\n")}));
    QCOMPARE(window.source(), QStringLiteral("aaa\nbbb\nccc\nddd"));
    QCOMPARE(DocumentSegmenter::assemble(windows, {}), document);

    const QStringList translated = {QStringLiteral("甲"), QStringLiteral("乙"),
                                    QStringLiteral("丙"), QStringLiteral("丁")};
    QCOMPARE(DocumentSegmenter::renderWindow(window, translated),
             QStringLiteral("甲\n\n\n乙\n丙\n\n丙\n丁\n"));

    // A line that returns after another is a line of its own; a fold reaches over
    // the blanks opening the document.
    const QVector<DocumentWindow> reopened =
        DocumentSegmenter::partition(QStringLiteral("aaa\nbbb\n\naaa\n"));
    QCOMPARE(reopened.first().lineCount(), 3);
    QCOMPARE(reopened.first().lines.at(2).whitespaceBefore, QStringList({QStringLiteral("\n\n")}));

    const QString opened = QStringLiteral("\n\nbbb\n \nbbb\n");
    const QVector<DocumentWindow> leadingBlanks = DocumentSegmenter::partition(opened);
    QCOMPARE(leadingBlanks.first().lineCount(), 1);
    QCOMPARE(leadingBlanks.first().lines.first().whitespaceBefore,
             QStringList({QStringLiteral("\n\n"), QStringLiteral("\n \n")}));
    QCOMPARE(DocumentSegmenter::assemble(leadingBlanks, {}), opened);
}

// The whitespace around a line stays out of the request, whatever it is made of,
// and comes back byte for byte.
void TestDocumentSegmenter::requestsLinesWithoutTheWhitespaceAroundThem()
{
    // What \s reads as whitespace: blanks, a no-break space, an ideographic space
    // and the separators.
    const QString leading = QStringLiteral("\t") + QChar(0x00a0) + QChar(0x3000) + QChar(0x0085)
                            + QChar(0x2028) + QChar(0x2029);
    const QString document = leading + QStringLiteral("first line\n"
                                                      "  second line \t\n"
                                                      "\v\fthird line  \n");

    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document);
    QCOMPARE(windows.size(), 1);
    const DocumentWindow& window = windows.first();

    // The model and the window both read the text alone.
    QCOMPARE(window.lines.at(0).text, QStringLiteral("first line"));
    QCOMPARE(window.lines.at(1).text, QStringLiteral("second line"));
    QCOMPARE(window.lines.at(2).text, QStringLiteral("third line"));
    QCOMPARE(window.source(), QStringLiteral("first line\nsecond line\nthird line"));

    const QStringList translated = {QStringLiteral("甲"), QStringLiteral("乙"),
                                    QStringLiteral("丙")};
    QCOMPARE(DocumentSegmenter::renderWindow(window, translated),
             leading + QStringLiteral("甲\n  乙 \t\n\v\f丙  \n"));

    // A line ends at the line break, so the CR of a CRLF pair joins the runs.
    const QVector<DocumentWindow> crlf =
        DocumentSegmenter::partition(QStringLiteral("a\r\n  b\r\n"));
    QCOMPARE(crlf.first().lines.at(1).whitespaceBefore, QStringList({QStringLiteral("\r\n  ")}));
    QCOMPARE(crlf.first().whitespaceAfter, QStringLiteral("\r\n"));
    QCOMPARE(DocumentSegmenter::assemble(crlf, {}), QStringLiteral("a\r\n  b\r\n"));
}

// A line of whitespace alone is never requested, and comes back as written rather
// than as an empty line.
void TestDocumentSegmenter::keepsBlankLinesOfWhitespaceAsWritten()
{
    // Blanks, a no-break space and an ideographic space between the lines.
    const QString blanks = QStringLiteral("\n    \n") + QChar(0x00a0) + QChar(0x3000)
                           + QStringLiteral("\n\t\n \t \n");
    const QString document = QStringLiteral("alpha") + blanks + QStringLiteral("beta\n");
    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document);
    QCOMPARE(windows.size(), 1);
    const DocumentWindow& window = windows.first();
    QCOMPARE(window.lineCount(), 2);
    QCOMPARE(window.lines.at(1).text, QStringLiteral("beta"));
    QCOMPARE(window.lines.at(1).whitespaceBefore, QStringList({blanks}));

    const QStringList translated = {QStringLiteral("甲"), QStringLiteral("乙")};
    QCOMPARE(DocumentSegmenter::renderWindow(window, translated),
             QStringLiteral("甲") + blanks + QStringLiteral("乙\n"));
    QCOMPARE(DocumentSegmenter::assemble(windows, {}), document);
}

// Lines indented differently are one request line, and every occurrence keeps the
// run it was written with.
void TestDocumentSegmenter::foldsLinesThatDifferOnlyInIndent()
{
    const QString document = QStringLiteral("Share the news\n"
                                            "    Share the news\n"
                                            "\tShare the news\n");
    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document);
    QCOMPARE(windows.size(), 1);
    const DocumentWindow& window = windows.first();
    QCOMPARE(window.lineCount(), 1);
    QCOMPARE(window.lines.first().whitespaceBefore,
             QStringList({QString(), QStringLiteral("\n    "), QStringLiteral("\n\t")}));
    QCOMPARE(window.whitespaceAfter, QStringLiteral("\n"));
    QCOMPARE(window.source(), QStringLiteral("Share the news"));

    const QStringList translated = {QStringLiteral("分享这条新闻")};
    QCOMPARE(DocumentSegmenter::renderWindow(window, translated),
             QStringLiteral("分享这条新闻\n    分享这条新闻\n\t分享这条新闻\n"));

    // A repeat opening the next window is a line of its own.
    const QVector<DocumentWindow> split =
        DocumentSegmenter::partition(QStringLiteral("Share the news\nShare the news\n"), 10, 1);
    QCOMPARE(split.size(), 2);
    QCOMPARE(split.at(1).lineCount(), 1);
    QCOMPARE(split.at(1).lines.first().whitespaceBefore, QStringList({QStringLiteral("\n")}));
}

// Lines closed with different whitespace are one request line, and every
// occurrence keeps its run.
void TestDocumentSegmenter::foldsLinesThatDifferOnlyInTrailingWhitespace()
{
    const QString document = QStringLiteral("Share the news  \n"
                                            "    Share the news\t\n"
                                            "Share the news \t \n"
                                            "tail\n");
    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document);
    QCOMPARE(windows.size(), 1);
    const DocumentWindow& window = windows.first();
    QCOMPARE(window.lineCount(), 2);
    QCOMPARE(window.lines.first().whitespaceBefore,
             QStringList({QString(), QStringLiteral("  \n    "), QStringLiteral("\t\n")}));
    QCOMPARE(window.source(), QStringLiteral("Share the news\ntail"));

    const QStringList translated = {QStringLiteral("分享这条新闻"), QStringLiteral("尾部")};
    QCOMPARE(DocumentSegmenter::renderWindow(window, translated),
             QStringLiteral("分享这条新闻  \n    分享这条新闻\t\n"
                            "分享这条新闻 \t \n尾部\n"));
}

// A document reaches the model without its whitespace and comes back whole across
// the windows it was split into.
void TestDocumentSegmenter::restoresWhitespaceAcrossWindows()
{
    const QString document = QStringLiteral("News  \n"
                                            "\n"
                                            "    Headline \t\n"
                                            "\tHeadline\n"
                                            "Headline\n"
                                            "\n"
                                            "    \t    \n"
                                            "\n"
                                            "Body text\t\n"
                                            "    ");
    // One line per window, so the whitespace before a window travels with it.
    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document, 2, 1);
    QVERIFY(windows.size() > 1);
    for (const DocumentWindow& window : windows) {
        for (const DocumentLine& line : window.lines) {
            QVERIFY2(!line.text.isEmpty() && !line.text.front().isSpace()
                         && !line.text.back().isSpace(),
                     qPrintable(line.text));
        }
    }

    QCOMPARE(DocumentSegmenter::assemble(windows, {}), document);
    QCOMPARE(DocumentSegmenter::assemble(windows, sourceLines(windows)), document);
}

void TestDocumentSegmenter::restoresDocumentFromTranslations()
{
    const QString document = QStringLiteral("  \n"
                                            "first line\n"
                                            "first line\n"
                                            "\n"
                                            "second line\n"
                                            "\n"
                                            "\n"
                                            "third line");
    // Two words per line, so a window of two words holds one line: the document
    // is restored across several windows.
    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document, 2, 10);
    QVERIFY(windows.size() >= 2);

    // The whitespace opening the document comes back as written, blank lines and
    // repeats included.
    const QString restored =
        DocumentSegmenter::assemble(windows, sourceLines(windows));
    QCOMPARE(restored, document);
}

void TestDocumentSegmenter::yieldsNoWindowForCaptionsAndBlankText()
{
    // A document whose lines carry no text yields no window.
    const QStringList documents = {QString(), QStringLiteral("\n"), QStringLiteral("   "),
                                   QStringLiteral("\n \n\t\n"), QStringLiteral("\r\n")};
    for (const QString& document : documents) {
        QVERIFY2(DocumentSegmenter::partition(document).isEmpty(), qPrintable(document));
    }

    // Blank lines carry no text, so they only keep the shape of the document.
    const QString document = QStringLiteral("\nalpha\n");
    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document);
    QCOMPARE(windows.size(), 1);
    QCOMPARE(windows.first().source(), QStringLiteral("alpha"));
    QCOMPARE(windows.first().lines.first().whitespaceBefore, QStringList({QStringLiteral("\n")}));
    QCOMPARE(windows.first().whitespaceAfter, QStringLiteral("\n"));
    QCOMPARE(DocumentSegmenter::assemble(windows, {}), document);

    // Blank lines never take part in the word limit.
    const QString blanks(Defaults::documentWindowWords, QLatin1Char('\n'));
    QVERIFY(DocumentSegmenter::partition(blanks).isEmpty());
}

// The line limit closes a window the word limit left short.
void TestDocumentSegmenter::endsWindowAtLineLimitBeforeWordLimit()
{
    QStringList lines;
    for (int index = 0; index < 23; ++index)
        lines << QStringLiteral("line %1").arg(index);

    const QVector<DocumentWindow> windows =
        DocumentSegmenter::partition(lines.join(QLatin1Char('\n')));
    QCOMPARE(windows.size(), 3);
    QCOMPARE(windows.at(0).lineCount(), Defaults::documentWindowLines);
    QCOMPARE(windows.at(1).lineCount(), Defaults::documentWindowLines);
    QCOMPARE(windows.at(2).lineCount(), 3);

    // Every line still reaches the model exactly once.
    QCOMPARE(DocumentSegmenter::assemble(windows, sourceLines(windows)),
             lines.join(QLatin1Char('\n')));
}

// The unbounded line limit leaves the line count open, so the word limit alone
// closes a window.
void TestDocumentSegmenter::splitsByConfiguredWindowBounds()
{
    // Distinct lines of one word each, so nothing collapses as a repeat.
    QStringList lines;
    for (int index = 0; index < 40; ++index)
        lines << QString::number(index).rightJustified(2, QLatin1Char('0'));
    const QString document = lines.join(QLatin1Char('\n'));

    // Well past the default line limit, a single window holds every line.
    const QVector<DocumentWindow> unbounded = DocumentSegmenter::partition(
        document, 1000, DocumentWindowLines::unlimitedSentinel);
    QCOMPARE(unbounded.size(), 1);
    QCOMPARE(unbounded.first().lineCount(), lines.size());
    QCOMPARE(unbounded.first().source(), document);

    // Three lines of one word each fill the limit, so it alone sets the window
    // size.
    const QVector<DocumentWindow> tight = DocumentSegmenter::partition(
        document, 3, DocumentWindowLines::unlimitedSentinel);
    QCOMPARE(tight.size(), 14);
    for (const DocumentWindow& window : tight)
        QVERIFY(TextUtils::wordCount(window.source()) <= 3);
    QCOMPARE(DocumentSegmenter::assemble(tight, sourceLines(tight)), document);
}

// Only the lines that reach the model count towards the limit.
void TestDocumentSegmenter::countsWordsAfterCollapsingRepeats()
{
    QStringList lines;
    const int words = 30;
    for (int index = 0; index < 6; ++index) {
        const QString repeated = wordLine(QStringLiteral("w%1").arg(index), words);
        for (int repeat = 0; repeat < 4; ++repeat)
            lines << repeated;
    }
    const QVector<DocumentWindow> windows =
        DocumentSegmenter::partition(lines.join(QLatin1Char('\n')), 2 * words + 1, 10);

    // The six distinct lines of 30 words collapse their repeats and fill three
    // windows of two lines each; the 24 occurrences reach the model once.
    QCOMPARE(windows.size(), 3);
    for (const DocumentWindow& window : windows) {
        QCOMPARE(window.lineCount(), 2);
        QCOMPARE(window.lines.first().whitespaceBefore.size(), 4);
    }
}

void TestDocumentSegmenter::rendersWindowWithRepeatsAndBlanks()
{
    const QVector<DocumentWindow> windows =
        DocumentSegmenter::partition(QStringLiteral("\nA\nA\n\nB\n"));
    QCOMPARE(windows.size(), 1);
    const DocumentWindow& window = windows.first();

    const QStringList translated = {QStringLiteral("甲"), QStringLiteral("乙")};
    const QString rendered = DocumentSegmenter::renderWindow(window, translated);
    QCOMPARE(rendered, QStringLiteral("\n甲\n甲\n\n乙\n"));
    QCOMPARE(DocumentSegmenter::assemble(windows, {translated}), rendered);

    // A window an answer covers only in part keeps its own text.
    QCOMPARE(DocumentSegmenter::renderWindow(window, {QStringLiteral("甲")}),
             QStringLiteral("\nA\nA\n\nB\n"));
    QCOMPARE(DocumentSegmenter::renderWindow(window, {}), QStringLiteral("\nA\nA\n\nB\n"));
    QCOMPARE(DocumentSegmenter::assemble(windows, {}), QStringLiteral("\nA\nA\n\nB\n"));
}

void TestDocumentSegmenter::splitsWindowByLineNumber()
{
    const DocumentWindow window =
        windowOf({QStringLiteral("first line"), QStringLiteral("second line")});

    const std::optional<QStringList> lines = DocumentSegmenter::splitTranslation(
        window, windowJson({QStringLiteral("第一行"), QStringLiteral("第二行")}));
    QVERIFY(lines.has_value());
    QCOMPARE(lines->size(), window.lineCount());
    QCOMPARE(*lines, QStringList({QStringLiteral("第一行"), QStringLiteral("第二行")}));

    // Indentation and blank lines between entries are the same object.
    const std::optional<QStringList> spaced = DocumentSegmenter::splitTranslation(
        window, QStringLiteral("{\n"
                               "    \"1\": \"第一行\",\n"
                               "\n"
                               "    \"2\": \"第二行\"\n"
                               "}"));
    QVERIFY(spaced.has_value());
    QCOMPARE(*spaced, QStringList({QStringLiteral("第一行"), QStringLiteral("第二行")}));

    // Whitespace around a value is not part of the translation.
    const std::optional<QStringList> padded = DocumentSegmenter::splitTranslation(
        window, QStringLiteral("{\"1\":\"  第一行  \",\"2\":\"\\t第二行 \"}"));
    QVERIFY(padded.has_value());
    QCOMPARE(*padded, QStringList({QStringLiteral("第一行"), QStringLiteral("第二行")}));

    // A one-line window is answered by a one-key object.
    const std::optional<QStringList> single = DocumentSegmenter::splitTranslation(
        windowOf({QStringLiteral("only line")}), QStringLiteral("{\"1\":\"单行\"}"));
    QVERIFY(single.has_value());
    QCOMPARE(*single, QStringList({QStringLiteral("单行")}));
}

void TestDocumentSegmenter::readsKeysInNumberOrder()
{
    const DocumentWindow window =
        windowOf({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});

    // The text order of the keys does not matter: the answer is read by number.
    const std::optional<QStringList> lines = DocumentSegmenter::splitTranslation(
        window, QStringLiteral("{\"3\":\"丙\",\"1\":\"甲\",\"2\":\"乙\"}"));
    QVERIFY(lines.has_value());
    QCOMPARE(*lines, QStringList({QStringLiteral("甲"), QStringLiteral("乙"), QStringLiteral("丙")}));

    // Values are matched to their own key rather than to their position.
    const std::optional<QStringList> shifted = DocumentSegmenter::splitTranslation(
        window, QStringLiteral("{\"2\":\"乙\",\"3\":\"丙\",\"1\":\"甲\"}"));
    QVERIFY(shifted.has_value());
    QCOMPARE(*shifted, QStringList({QStringLiteral("甲"), QStringLiteral("乙"), QStringLiteral("丙")}));
}

void TestDocumentSegmenter::acceptsAnEchoWrappedInLines()
{
    const DocumentWindow window =
        windowOf({QStringLiteral("first line"), QStringLiteral("second line")});

    // A model naming the object it echoes is still understood.
    const std::optional<QStringList> lines = DocumentSegmenter::splitTranslation(
        window, QStringLiteral("{\"lines\":{\"1\":\"第一行\",\"2\":\"第二行\"}}"));
    QVERIFY(lines.has_value());
    QCOMPARE(*lines, QStringList({QStringLiteral("第一行"), QStringLiteral("第二行")}));

    const std::optional<QStringList> translations = DocumentSegmenter::splitTranslation(
        window, QStringLiteral("{\"translations\":{\"1\":\"第一行\",\"2\":\"第二行\"}}"));
    QVERIFY(translations.has_value());
    QCOMPARE(*translations,
             QStringList({QStringLiteral("第一行"), QStringLiteral("第二行")}));
}

void TestDocumentSegmenter::rejectsInvalidKeySets()
{
    const DocumentWindow window =
        windowOf({QStringLiteral("first line"), QStringLiteral("second line")});

    // A missing, empty, repeated, extra or non-numeric key breaks the mapping.
    const QStringList responses = {
        QStringLiteral("{\"1\":\"第一行\"}"),
        QStringLiteral("{}"),
        QStringLiteral("{\"1\":\"第一行\",\"2\":\"\"}"),
        QStringLiteral("{\"1\":\"第一行\",\"2\":\"   \"}"),
        QStringLiteral("{\"1\":null,\"2\":\"第二行\"}"),
        QStringLiteral("{\"1\":\"第一行\",\"1\":\"第二行\",\"2\":\"第三行\"}"),
        QStringLiteral("{\"1\":\"第一行\",\"2\":\"第二行\",\"3\":\"第三行\"}"),
        QStringLiteral("{\"0\":\"第一行\",\"1\":\"第二行\"}"),
        QStringLiteral("{\"1\":\"第一行\",\"a\":\"第二行\"}"),
        QStringLiteral("{\"lines\":{\"1\":\"第一行\"}}"),
        QStringLiteral("{\"lines\":{}}")
    };
    for (const QString& response : responses) {
        QVERIFY2(!DocumentSegmenter::splitTranslation(window, response).has_value(),
                 qPrintable(response));
    }
}

void TestDocumentSegmenter::rejectsAnswersThatAreNotAWindow()
{
    const DocumentWindow window =
        windowOf({QStringLiteral("first line"), QStringLiteral("second line")});

    // A plain-text answer is not a shape a window accepts.
    const QStringList responses = {
        QStringLiteral("第一行\n第二行"),
        QStringLiteral("[\"第一行\",\"第二行\"]"),
        QStringLiteral("{1:第一行,2:第二行}"),
        QStringLiteral("{\"1\":1,\"2\":2}"),
        QString(),
        QStringLiteral("   ")
    };
    for (const QString& response : responses) {
        QVERIFY2(!DocumentSegmenter::splitTranslation(window, response).has_value(),
                 qPrintable(response));
    }
}

// A long answer of quoted words names no line, and the scan gives up on it.
void TestDocumentSegmenter::givesUpOnAnAnswerWithoutEntries()
{
    const DocumentWindow window = windowOf({QStringLiteral("a"), QStringLiteral("b")});

    QStringList words;
    for (int index = 0; index < 2000; ++index)
        words << QStringLiteral("\"word %1\"").arg(index);

    // The comma closing the answer is its only punctuation, so every candidate
    // key reads up to it; without it the whole answer is one unterminated run.
    const QString quoted = words.join(QLatin1Char(' '));
    QVERIFY(quoted.size() > 20000);
    QVERIFY(!DocumentSegmenter::splitTranslation(window, quoted + QLatin1Char(',')).has_value());
    QVERIFY(!DocumentSegmenter::splitTranslation(window, quoted).has_value());
}

void TestDocumentSegmenter::decodesEscapedValues()
{
    const DocumentWindow window = windowOf({QStringLiteral("a"), QStringLiteral("b"),
                                            QStringLiteral("c"), QStringLiteral("d")});

    // Quotes, a backslash, a tab and a code point all arrive decoded.
    const std::optional<QStringList> lines = DocumentSegmenter::splitTranslation(
        window,
        QStringLiteral(R"({"1":"He said \"hi\"","2":"C:\\path\\file","3":"a\tb","4":"\u4e2d\u6587"})"));
    QVERIFY(lines.has_value());
    QCOMPARE(*lines, QStringList({QStringLiteral("He said \"hi\""),
                                  QStringLiteral("C:\\path\\file"),
                                  QStringLiteral("a\tb"), QStringLiteral("中文")}));
}

void TestDocumentSegmenter::keepsNewlineInsideValue()
{
    const DocumentWindow window =
        windowOf({QStringLiteral("first line"), QStringLiteral("second line")});

    // An unescaped newline belongs to the value; it does not split the answer.
    const std::optional<QStringList> lines = DocumentSegmenter::splitTranslation(
        window, QStringLiteral("{\"1\":\"第一行\n续行\",\"2\":\"第二行\"}"));
    QVERIFY(lines.has_value());
    QCOMPARE(*lines, QStringList({QStringLiteral("第一行\n续行"), QStringLiteral("第二行")}));
}

// A model that leaves a value's quotes unescaped still gets the whole line
// across: a quote stays in the value while no entry follows the punctuation
// behind it.
void TestDocumentSegmenter::readsQuotesTheAnswerLeftUnescaped()
{
    const DocumentWindow window =
        windowOf({QStringLiteral("He said \"hi\", then left."),
                  QStringLiteral("Use print(\"hello\", \"world\")."),
                  QStringLiteral("Time is \"12:30\" now.")});

    const std::optional<QStringList> lines = DocumentSegmenter::splitTranslation(
        window,
        QStringLiteral("{\"1\": \"He said \"hi\", then left.\",\n"
                       " \"2\": \"使用 print(\"hello\", \"world\")。\",\n"
                       " \"3\": \"现在是 \"12:30\"。\"}"));
    QVERIFY(lines.has_value());
    QCOMPARE(*lines, QStringList({QStringLiteral("He said \"hi\", then left."),
                                  QStringLiteral("使用 print(\"hello\", \"world\")。"),
                                  QStringLiteral("现在是 \"12:30\"。")}));

    // A lone entry has no next entry to look for behind its comma.
    const std::optional<QStringList> lone = DocumentSegmenter::splitTranslation(
        windowOf({QStringLiteral("a")}), QStringLiteral("{\"1\": \"He said \"hi\", then left.\"}"));
    QVERIFY(lone.has_value());
    QCOMPARE(*lone, QStringList({QStringLiteral("He said \"hi\", then left.")}));

    // The quote closing a value keeps ending it, so the keys stay whole.
    const std::optional<QStringList> pair = DocumentSegmenter::splitTranslation(
        windowOf({QStringLiteral("a"), QStringLiteral("b")}),
        QStringLiteral("{\"1\": \"他说：\"你好\"\", \"2\": \"第二行\"}"));
    QVERIFY(pair.has_value());
    QCOMPARE(*pair, QStringList({QStringLiteral("他说：\"你好\""), QStringLiteral("第二行")}));
}

// A model that drops the quote closing a value leaves it unclosed; its text
// runs up to the entry behind it, so the line is not lost.
void TestDocumentSegmenter::salvagesValuesTheAnswerLeftUnclosed()
{
    const DocumentWindow window = windowOf({QStringLiteral("a"), QStringLiteral("b"),
                                            QStringLiteral("c")});

    const std::optional<QStringList> lines = DocumentSegmenter::splitTranslation(
        window,
        QStringLiteral("{\n"
                       "    \"1\": \"第一行\",\n"
                       "    \"2\": \"他说：‘你好’。”,\n"
                       "    \"3\": \"第三行\"\n"
                       "}"));
    QVERIFY(lines.has_value());
    QCOMPARE(*lines, QStringList({QStringLiteral("第一行"), QStringLiteral("他说：‘你好’。”"),
                                  QStringLiteral("第三行")}));

    // The comma separating the two members is not part of the value, and a
    // value left unclosed at the end of the answer ends with the text.
    const std::optional<QStringList> last = DocumentSegmenter::splitTranslation(
        windowOf({QStringLiteral("a"), QStringLiteral("b")}),
        QStringLiteral("{\"1\": \"第一行\", \"2\": \"他说：‘你好’。”}"));
    QVERIFY(last.has_value());
    QCOMPARE(*last, QStringList({QStringLiteral("第一行"), QStringLiteral("他说：‘你好’。”")}));

    // Only an entry the answer still owes ends a value, so a quoted word with a
    // colon behind it stays part of the line it was written in.
    const std::optional<QStringList> keyed = DocumentSegmenter::splitTranslation(
        windowOf({QStringLiteral("a"), QStringLiteral("b")}),
        QStringLiteral("{\"1\": \"字段 \"code\": \"x\" 结束\", \"2\": \"第二行\"}"));
    QVERIFY(keyed.has_value());
    QCOMPARE(*keyed, QStringList({QStringLiteral("字段 \"code\": \"x\" 结束"),
                                  QStringLiteral("第二行")}));
}

void TestDocumentSegmenter::mapsRepeatedLinesOntoFoldedLines()
{
    const QVector<DocumentWindow> windows =
        DocumentSegmenter::partition(QStringLiteral("alpha\nbeta\nbeta\ngamma"));
    QCOMPARE(windows.size(), 1);
    const DocumentWindow& window = windows.first();
    QCOMPARE(window.lineCount(), 3);
    QCOMPARE(window.source(), QStringLiteral("alpha\nbeta\ngamma"));

    // A repeated line reaches the model once, so the answer holds three entries.
    const std::optional<QStringList> lines = DocumentSegmenter::splitTranslation(
        window, QStringLiteral("{\"1\":\"甲\",\"2\":\"乙\",\"3\":\"丙\"}"));
    QVERIFY(lines.has_value());
    QCOMPARE(lines->size(), window.lineCount());
    QCOMPARE(*lines, QStringList({QStringLiteral("甲"), QStringLiteral("乙"), QStringLiteral("丙")}));
    // The translation of the repeated line is written back at every occurrence.
    QCOMPARE(DocumentSegmenter::renderWindow(window, *lines), QStringLiteral("甲\n乙\n乙\n丙"));

    // Every repeat answered once more than the window holds is not an answer.
    QVERIFY(!DocumentSegmenter::splitTranslation(
                 window, QStringLiteral("{\"1\":\"甲\",\"2\":\"乙\",\"3\":\"乙\",\"4\":\"丙\"}"))
                 .has_value());
}

void TestDocumentSegmenter::stripsCodeFenceFromAnswer()
{
    const DocumentWindow window =
        windowOf({QStringLiteral("first line"), QStringLiteral("second line")});

    const std::optional<QStringList> lines = DocumentSegmenter::splitTranslation(
        window, QStringLiteral("```json\n"
                               "{\"1\":\"第一行\",\"2\":\"第二行\"}\n"
                               "```"));
    QVERIFY(lines.has_value());
    QCOMPARE(*lines, QStringList({QStringLiteral("第一行"), QStringLiteral("第二行")}));

    const std::optional<QStringList> untagged = DocumentSegmenter::splitTranslation(
        window, QStringLiteral("```\n"
                               "{\"1\":\"第一行\",\"2\":\"第二行\"}\n"
                               "```"));
    QVERIFY(untagged.has_value());
    QCOMPARE(*untagged, QStringList({QStringLiteral("第一行"), QStringLiteral("第二行")}));
}

void TestDocumentSegmenter::parsesAnswerOfAWindowOfFences()
{
    // A window of the document may itself be fence lines; they are ordinary text.
    const DocumentWindow window =
        DocumentSegmenter::partition(QStringLiteral("```\ncode line\n```")).first();
    QCOMPARE(window.lineCount(), 3);

    const std::optional<QStringList> lines = DocumentSegmenter::splitTranslation(
        window, QStringLiteral("{\"1\":\"```\",\"2\":\"代码行\",\"3\":\"```\"}"));
    QVERIFY(lines.has_value());
    QCOMPARE(*lines, QStringList({QStringLiteral("```"), QStringLiteral("代码行"),
                                  QStringLiteral("```")}));

    // Fences inside the values are not read as a wrapper around the answer.
    const std::optional<QStringList> wrapped = DocumentSegmenter::splitTranslation(
        window, QStringLiteral("```json\n"
                               "{\"1\":\"```\",\"2\":\"代码行\",\"3\":\"```\"}\n"
                               "```"));
    QVERIFY(wrapped.has_value());
    QCOMPARE(*wrapped, QStringList({QStringLiteral("```"), QStringLiteral("代码行"),
                                    QStringLiteral("```")}));
}

QTEST_MAIN(TestDocumentSegmenter)
#include "tst_documentsegmenter.moc"
