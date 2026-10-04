#include <QtTest>

#include "core/config/Defaults.h"
#include "core/document/DocumentSegmenter.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <optional>

namespace {

// One translated line per window line, so assembling must reproduce the document.
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

// Window holding \p lines once each, without blank lines around them.
DocumentWindow windowOf(const QStringList& lines)
{
    DocumentWindow window;
    for (const QString& line : lines)
        window.lines.append({line, {0}});
    return window;
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
    void packsByCharactersWhenLinesAllow();
    void fillsWindowsUpToTheDefaultCharacterLimit();
    void fitsExactlyTheCharacterLimit();
    void keepsOversizedLineInItsOwnWindow();
    void recordsBlankLinesAndRepeats();
    void foldsRepeatsAcrossBlankLines();
    void keepsBlanksAFoldDidNotBridge();
    void restoresDocumentFromTranslations();
    void yieldsNoWindowForCaptionsAndBlankText();
    void endsWindowAtLineLimitBeforeCharacterLimit();
    void countsCharactersAfterCollapsingRepeats();
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

// With the lines to spare, the character limit is what closes a window.
void TestDocumentSegmenter::packsByCharactersWhenLinesAllow()
{
    QStringList lines;
    for (int index = 0; index < 25; ++index)
        lines << QString::number(index).rightJustified(3, QLatin1Char('0'))
              + QStringLiteral(" abcdefghijklm");
    const QString document = lines.join(QLatin1Char('\n'));

    // 17 characters per line plus the newline separating them: five lines fill
    // 89 characters, a sixth would reach 107.
    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document, 100, 100);
    QCOMPARE(windows.size(), 5);
    for (const DocumentWindow& window : windows)
        QCOMPARE(window.lineCount(), 5);

    // Every line reaches the model exactly once, and no window overshoots.
    QCOMPARE(DocumentSegmenter::assemble(windows, sourceLines(windows)), document);
    for (const DocumentWindow& window : windows) {
        int characters = window.lineCount() - 1;
        for (const DocumentLine& line : window.lines)
            characters += line.text.size();
        QVERIFY2(characters <= 100, qPrintable(window.source()));
    }
}

// Lines that sum to the default limit fill a window; one character more opens
// the next.
void TestDocumentSegmenter::fillsWindowsUpToTheDefaultCharacterLimit()
{
    // Two lines whose joined text is exactly the limit stay together.
    const QString exact = QStringLiteral("1\n") + QString(498, QLatin1Char('a'));
    const QVector<DocumentWindow> fitted = DocumentSegmenter::partition(exact);
    QCOMPARE(fitted.size(), 1);
    QCOMPARE(fitted.first().lineCount(), 2);
    QCOMPARE(fitted.first().source().size(), Defaults::documentWindowCharacters);

    // One character more on the long line puts it in a window of its own.
    const QString over = QStringLiteral("11\n") + QString(498, QLatin1Char('a'));
    const QVector<DocumentWindow> split = DocumentSegmenter::partition(over);
    QCOMPARE(split.size(), 2);
    QCOMPARE(split.at(0).source(), QStringLiteral("11"));
    QCOMPARE(split.at(1).source(), QString(498, QLatin1Char('a')));

    // 500 short lines reach the window's line limit long before its character
    // limit.
    QStringList many;
    for (int index = 0; index < Defaults::documentWindowCharacters; ++index)
        many << QStringLiteral("x%1").arg(index);
    const QVector<DocumentWindow> capped =
        DocumentSegmenter::partition(many.join(QLatin1Char('\n')));
    QCOMPARE(capped.size(), 50);
    for (const DocumentWindow& window : capped)
        QCOMPARE(window.lineCount(), Defaults::documentWindowLines);
    QCOMPARE(DocumentSegmenter::assemble(capped, sourceLines(capped)),
             many.join(QLatin1Char('\n')));
}

// A window fills up to the limit without crossing it.
void TestDocumentSegmenter::fitsExactlyTheCharacterLimit()
{
    const QString document = QStringLiteral("1111111111\n"
                                            "2222222222\n"
                                            "3333333333");

    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document, 21, 10);
    QCOMPARE(windows.size(), 2);
    QCOMPARE(windows.at(0).lineCount(), 2);
    QCOMPARE(windows.at(0).source(), QStringLiteral("1111111111\n2222222222"));
    QCOMPARE(windows.at(1).source(), QStringLiteral("3333333333"));

    // A single line whose own text is the limit closes alone.
    const QVector<DocumentWindow> single =
        DocumentSegmenter::partition(QStringLiteral("111111111111111111111"), 21, 10);
    QCOMPARE(single.size(), 1);
    QCOMPARE(single.first().lineCount(), 1);
}

// A line over the character limit fills a window on its own, and one over the
// line limit leaves with the lines that fitted before it.
void TestDocumentSegmenter::keepsOversizedLineInItsOwnWindow()
{
    const QString oversized(QString(501, QLatin1Char('a')));
    const QString document = QStringLiteral("a b c\n") + oversized + QStringLiteral("\na b c");

    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document);
    QCOMPARE(windows.size(), 3);
    QCOMPARE(windows.at(0).source(), QStringLiteral("a b c"));
    QCOMPARE(windows.at(1).lineCount(), 1);
    QCOMPARE(windows.at(1).source(), oversized);
    QCOMPARE(windows.at(2).source(), QStringLiteral("a b c"));

    const QString tenLines = QStringLiteral("a\nb\nc\nd\ne\nf\ng\nh\ni\n")
                             + QString(150, QLatin1Char('z'));
    const QVector<DocumentWindow> capped = DocumentSegmenter::partition(tenLines, 100, 10);
    QCOMPARE(capped.size(), 2);
    QCOMPARE(capped.at(0).lineCount(), 9);
    QCOMPARE(capped.at(1).source(), QString(150, QLatin1Char('z')));

    // Two lines that together cross the limit do not share a window, even
    // though each of them fits it alone, while a short line leaves room for one
    // that crosses it.
    const QString first(QString(300, QLatin1Char('a')));
    const QString second(QString(300, QLatin1Char('b')));
    const QVector<DocumentWindow> pair =
        DocumentSegmenter::partition(first + QStringLiteral("\n") + second);
    QCOMPARE(pair.size(), 2);
    QCOMPARE(pair.at(0).source(), first);
    QCOMPARE(pair.at(1).source(), second);

    const QString shortLine(QString(100, QLatin1Char('s')));
    const QVector<DocumentWindow> mixed =
        DocumentSegmenter::partition(shortLine + QStringLiteral("\n") + first);
    QCOMPARE(mixed.size(), 1);
    QCOMPARE(mixed.first().lineCount(), 2);
    QCOMPARE(mixed.first().source().size(), 401);
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
    QCOMPARE(window.lines.at(0).blanksBefore, QVector<int>({0}));
    QCOMPARE(window.lines.at(1).text, QStringLiteral("beta two"));
    QCOMPARE(window.lines.at(1).blanksBefore, QVector<int>({1, 0, 0}));
    QCOMPARE(window.lines.at(2).blanksBefore, QVector<int>({2}));
    QCOMPARE(window.trailingBlanks, 1);

    // The collapsed document reaches the model with one entry per line, and
    // comes back from that entry with every occurrence and blank line in place.
    QCOMPARE(window.source(), QStringLiteral("alpha one\nbeta two\ngamma three"));
    QCOMPARE(DocumentSegmenter::assemble(windows, {}),
             QStringLiteral("alpha one\n\nbeta two\nbeta two\nbeta two\n\n\ngamma three\n"));
}

// Blank lines carry no text, so they do not separate two equal lines: the
// folding runs over the document without them.
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

    // The four occurrences reach the model as one line, and every one of them
    // keeps the blank lines that stood before it.
    QCOMPARE(window.lineCount(), 3);
    QCOMPARE(window.lines.at(0).text, QStringLiteral("aaa"));
    QCOMPARE(window.lines.at(1).text, QStringLiteral("bbb"));
    QCOMPARE(window.lines.at(1).blanksBefore, QVector<int>({0, 1, 1, 2}));
    QCOMPARE(window.lines.at(2).text, QStringLiteral("ccc"));
    QCOMPARE(window.source(), QStringLiteral("aaa\nbbb\nccc"));

    // Writing an answer back puts the line where the document wrote it, blank
    // lines and all; a line of spaces reads as a blank one.
    const QStringList translated = {QStringLiteral("甲"), QStringLiteral("乙"),
                                    QStringLiteral("丙")};
    const QString rendered = DocumentSegmenter::renderWindow(window, translated);
    QCOMPARE(rendered, QStringLiteral("甲\n乙\n\n乙\n\n乙\n\n\n乙\n丙\n"));
    QCOMPARE(DocumentSegmenter::assemble(windows, {translated}), rendered);
    QCOMPARE(DocumentSegmenter::assemble(windows, {}),
             QStringLiteral("aaa\nbbb\n\nbbb\n\nbbb\n\n\nbbb\nccc\n"));
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
    QCOMPARE(window.lines.at(1).blanksBefore, QVector<int>({2}));
    QCOMPARE(window.lines.at(2).blanksBefore, QVector<int>({0, 1}));
    QCOMPARE(window.source(), QStringLiteral("aaa\nbbb\nccc\nddd"));
    QCOMPARE(DocumentSegmenter::assemble(windows, {}), document);

    const QStringList translated = {QStringLiteral("甲"), QStringLiteral("乙"),
                                    QStringLiteral("丙"), QStringLiteral("丁")};
    QCOMPARE(DocumentSegmenter::renderWindow(window, translated),
             QStringLiteral("甲\n\n\n乙\n丙\n\n丙\n丁\n"));

    // A line that comes back after another line is a line of its own, and a fold
    // reaches over the blanks that open the document.
    const QVector<DocumentWindow> reopened =
        DocumentSegmenter::partition(QStringLiteral("aaa\nbbb\n\naaa\n"));
    QCOMPARE(reopened.first().lineCount(), 3);
    QCOMPARE(reopened.first().lines.at(2).blanksBefore, QVector<int>({1}));

    const QVector<DocumentWindow> opened =
        DocumentSegmenter::partition(QStringLiteral("\n\nbbb\n \nbbb\n"));
    QCOMPARE(opened.first().lineCount(), 1);
    QCOMPARE(opened.first().lines.first().blanksBefore, QVector<int>({2, 1}));
    QCOMPARE(DocumentSegmenter::assemble(opened, {}), QStringLiteral("\n\nbbb\n\nbbb\n"));
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
    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document, 12, 10);
    QVERIFY(windows.size() >= 2);

    // Leading blanks normalise, and every line comes back: repeats and blanks by count.
    const QString restored =
        DocumentSegmenter::assemble(windows, sourceLines(windows));
    QCOMPARE(restored, QStringLiteral("\n"
                                      "first line\n"
                                      "first line\n"
                                      "\n"
                                      "second line\n"
                                      "\n"
                                      "\n"
                                      "third line"));
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
    const QVector<DocumentWindow> windows =
        DocumentSegmenter::partition(QStringLiteral("\nalpha\n"));
    QCOMPARE(windows.size(), 1);
    QCOMPARE(windows.first().source(), QStringLiteral("alpha"));
    QCOMPARE(windows.first().lines.first().blanksBefore, QVector<int>({1}));
    QCOMPARE(windows.first().trailingBlanks, 1);
    QCOMPARE(DocumentSegmenter::assemble(windows, {}), QStringLiteral("\nalpha\n"));

    // Blank lines never take part in the character limit.
    const QString blanks(Defaults::documentWindowCharacters, QLatin1Char('\n'));
    QVERIFY(DocumentSegmenter::partition(blanks).isEmpty());
}

// The line limit closes a window the character limit left short.
void TestDocumentSegmenter::endsWindowAtLineLimitBeforeCharacterLimit()
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

// The unbounded line limit leaves the line count open, so the character limit
// alone closes a window.
void TestDocumentSegmenter::splitsByConfiguredWindowBounds()
{
    // Distinct lines of two characters each, so nothing collapses as a repeat.
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

    // Three lines of two characters plus the newlines between them fill 8
    // characters, so the character limit alone sets the window size.
    const QVector<DocumentWindow> tight = DocumentSegmenter::partition(
        document, 8, DocumentWindowLines::unlimitedSentinel);
    QCOMPARE(tight.size(), 14);
    for (const DocumentWindow& window : tight)
        QVERIFY(window.source().size() <= 8);
    QCOMPARE(DocumentSegmenter::assemble(tight, sourceLines(tight)), document);
}

// Only the lines that reach the model count towards the limit.
void TestDocumentSegmenter::countsCharactersAfterCollapsingRepeats()
{
    QStringList lines;
    const int length = 30;
    for (int index = 0; index < 6; ++index) {
        const QString repeated = QString(QLatin1Char('a' + index)).repeated(length);
        for (int repeat = 0; repeat < 4; ++repeat)
            lines << repeated;
    }
    const QVector<DocumentWindow> windows =
        DocumentSegmenter::partition(lines.join(QLatin1Char('\n')), 2 * length + 1, 10);

    // The six distinct lines of 30 characters collapse their repeats and fill
    // three windows of two lines each; the 24 occurrences are longer than that
    // but reach the model once per line.
    QCOMPARE(windows.size(), 3);
    for (const DocumentWindow& window : windows) {
        QCOMPARE(window.lineCount(), 2);
        QCOMPARE(window.lines.first().blanksBefore.size(), 4);
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

    // A window the answer did not cover keeps its own text.
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

    // The former plain-text answer is no longer a shape a window accepts.
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
