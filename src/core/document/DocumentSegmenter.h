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
    // Characters a window may hold, and the lines it may hold at most.
    static constexpr int windowCharacters = 500;
    static constexpr int windowLines = 10;

    // Splits \p document into windows of whole lines, taking lines while they fit
    // \p charLimit characters and never holding more than \p lineLimit lines. A
    // line longer than \p charLimit fills a window on its own. A document without
    // a line carrying text yields no window.
    static QVector<DocumentWindow> partition(const QString& document,
                                             int charLimit = windowCharacters,
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
