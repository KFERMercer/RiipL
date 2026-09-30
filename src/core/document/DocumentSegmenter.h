#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

// One line of a document: adjacent duplicates collapse into one entry, and the
// blank lines before it are recorded so they can be put back on export.
struct DocumentLine
{
    QString text;
    int repeats = 1;
    int blanksBefore = 0;
};

// One translation window: whole lines only, so a line is never split across two
// requests.
struct DocumentWindow
{
    QVector<DocumentLine> lines;
    // Blank lines closing the document; only the last window carries them.
    int trailingBlanks = 0;

    QString source() const;
    // Lines the window holds, duplicates already collapsed; the numbers an
    // answer is keyed by.
    int lineCount() const { return lines.size(); }
};

// Splits a document into translation windows and rebuilds it from the
// translated lines.
class DocumentSegmenter
{
public:
    // Words a window aims for, the count it may reach by taking the line that
    // crosses it, and the lines it may hold at most.
    static constexpr int windowWords = 500;
    static constexpr int snapWords = 600;
    static constexpr int windowLines = 10;

    // Splits \p document into windows of whole lines, about \p wordLimit words
    // each and never more than \p lineLimit lines. The line that crosses the word
    // limit is taken while the window stays within \p snapLimit, and opens the
    // next window past it. A document without a line carrying text yields no
    // window.
    static QVector<DocumentWindow> partition(const QString& document,
                                             int wordLimit = windowWords,
                                             int snapLimit = snapWords,
                                             int lineLimit = windowLines);
    // Answers, one per window line, keyed in the response by the line number
    // 1..lineCount(); nothing when the response does not hold those lines, each
    // once and carrying text.
    static std::optional<QStringList> splitTranslation(const DocumentWindow& window,
                                                       const QString& response);
    // Text of one window as it reads in the document, repeats and blank lines
    // included. A window without a full answer keeps its source text.
    static QString renderWindow(const DocumentWindow& window, const QStringList& translatedLines);
    // Rebuilds the document from one line list per window.
    static QString assemble(const QVector<DocumentWindow>& windows,
                            const QVector<QStringList>& translations);
};
