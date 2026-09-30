#include <QtTest>

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
        window.lines.append({line, 1, 0});
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
    void packsByWordsWhenLinesAllow();
    void limitsWindowsToTenLines();
    void snapsLineWithinSnapLimit();
    void movesLinePastSnapLimitToNextWindow();
    void keepsOversizedLineInItsOwnWindow();
    void recordsBlankLinesAndRepeats();
    void keepsBlankLineBetweenEqualLines();
    void restoresDocumentFromTranslations();
    void rendersWindowWithRepeatsAndBlanks();
    void splitsWindowByLineNumber();
    void readsKeysInNumberOrder();
    void acceptsAnEchoWrappedInLines();
    void rejectsInvalidKeySets();
    void rejectsAnswersThatAreNotAWindow();
    void decodesEscapedValues();
    void keepsNewlineInsideValue();
    void mapsRepeatedLinesOntoFoldedLines();
    void stripsCodeFenceFromAnswer();
    void parsesAnswerOfAWindowOfFences();
};

void TestDocumentSegmenter::partitionsIntoWholeLineWindows()
{
    QStringList lines;
    for (int index = 0; index < 25; ++index)
        lines << QStringLiteral("l%1 a b c d e f g h i j k").arg(index);
    const QString document = lines.join(QLatin1Char('\n'));

    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document);
    QCOMPARE(windows.size(), 3);
    QCOMPARE(windows.at(0).lineCount(), DocumentSegmenter::windowLines);
    QCOMPARE(windows.at(1).lineCount(), DocumentSegmenter::windowLines);
    QCOMPARE(windows.at(2).lineCount(), 5);
    QVERIFY(windows.at(0).source().startsWith(QStringLiteral("l0 a b")));

    // Whole lines only, and never more than a window may hold.
    int start = 0;
    for (const DocumentWindow& window : windows) {
        QVERIFY(window.lineCount() <= DocumentSegmenter::windowLines);
        QCOMPARE(window.source(), lines.mid(start, window.lineCount()).join(QLatin1Char('\n')));
        start += window.lineCount();
        QVERIFY(!window.source().isEmpty());
    }
    QCOMPARE(start, lines.size());
}

// With the lines to spare, the word target is what closes a window.
void TestDocumentSegmenter::packsByWordsWhenLinesAllow()
{
    QStringList lines;
    for (int index = 0; index < 100; ++index)
        lines << QStringLiteral("l%1 a b c d e f g h i j k").arg(index);
    const QString document = lines.join(QLatin1Char('\n'));

    const QVector<DocumentWindow> windows =
        DocumentSegmenter::partition(document, DocumentSegmenter::windowWords,
                                     DocumentSegmenter::snapWords, 100);
    QCOMPARE(windows.size(), 3);
    QCOMPARE(windows.at(0).lineCount(), 42);
    QCOMPARE(windows.at(1).lineCount(), 42);
    QCOMPARE(windows.at(2).lineCount(), 16);
}

void TestDocumentSegmenter::limitsWindowsToTenLines()
{
    QStringList lines;
    for (int index = 0; index < 23; ++index)
        lines << QStringLiteral("line %1 carries a few words").arg(index);

    // Short lines never reach the word target, so only the line limit closes windows.
    const QVector<DocumentWindow> windows =
        DocumentSegmenter::partition(lines.join(QLatin1Char('\n')));
    QCOMPARE(windows.size(), 3);
    QCOMPARE(windows.at(0).lineCount(), 10);
    QCOMPARE(windows.at(1).lineCount(), 10);
    QCOMPARE(windows.at(2).lineCount(), 3);

    // Every line still reaches the model exactly once.
    QCOMPARE(DocumentSegmenter::assemble(windows, sourceLines(windows)),
             lines.join(QLatin1Char('\n')));
}

void TestDocumentSegmenter::snapsLineWithinSnapLimit()
{
    const QString document = QStringLiteral("a b c d\n"
                                            "e f g h\n"
                                            "i j k l");

    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document, 10, 12);
    QCOMPARE(windows.size(), 1);
    QCOMPARE(windows.first().lineCount(), 3);
}

void TestDocumentSegmenter::movesLinePastSnapLimitToNextWindow()
{
    const QString document = QStringLiteral("a b c d\n"
                                            "e f g h\n"
                                            "i j k l m n");

    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document, 10, 12);
    QCOMPARE(windows.size(), 2);
    QCOMPARE(windows.at(0).source(), QStringLiteral("a b c d\ne f g h"));
    QCOMPARE(windows.at(1).source(), QStringLiteral("i j k l m n"));
}

void TestDocumentSegmenter::keepsOversizedLineInItsOwnWindow()
{
    const QString oversized = QStringLiteral("a b c d e f g h i j k l m n o p q r s t u v w x");
    const QString document = QStringLiteral("a b c\n") + oversized + QStringLiteral("\na b c");

    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document, 4, 6);
    QCOMPARE(windows.size(), 3);
    QCOMPARE(windows.at(0).source(), QStringLiteral("a b c"));
    QCOMPARE(windows.at(1).lineCount(), 1);
    QCOMPARE(windows.at(1).source(), oversized);
    QCOMPARE(windows.at(2).source(), QStringLiteral("a b c"));
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
    QCOMPARE(window.lines.at(0).blanksBefore, 0);
    QCOMPARE(window.lines.at(0).repeats, 1);
    QCOMPARE(window.lines.at(1).text, QStringLiteral("beta two"));
    QCOMPARE(window.lines.at(1).repeats, 3);
    QCOMPARE(window.lines.at(2).blanksBefore, 2);
    QCOMPARE(window.trailingBlanks, 1);

    // The collapsed document reaches the model with one entry per line.
    QCOMPARE(window.source(), QStringLiteral("alpha one\nbeta two\ngamma three"));
}

void TestDocumentSegmenter::keepsBlankLineBetweenEqualLines()
{
    const QString document = QStringLiteral("same line\n"
                                            "same line\n"
                                            "\n"
                                            "same line");

    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document);
    QCOMPARE(windows.size(), 1);
    const DocumentWindow& window = windows.first();
    QCOMPARE(window.lineCount(), 2);
    QCOMPARE(window.lines.at(0).repeats, 2);
    QCOMPARE(window.lines.at(1).text, QStringLiteral("same line"));
    QCOMPARE(window.lines.at(1).repeats, 1);
    QCOMPARE(window.lines.at(1).blanksBefore, 1);
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
    const QVector<DocumentWindow> windows = DocumentSegmenter::partition(document, 4, 6);
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
