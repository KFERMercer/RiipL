#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

#include "core/config/Defaults.h"

namespace DocumentWindowLines {

inline const int unlimitedSentinel = 0;

}

// One line of a document: its text, and the whitespace before every occurrence.
struct DocumentLine
{
    QString text;
    // Line breaks and blank lines are part of the run.
    QStringList whitespaceBefore;
};

// One translation window: whole lines only, never split across two requests.
struct DocumentWindow
{
    QVector<DocumentLine> lines;
    // Whitespace closing the document; the last window sets it.
    QString whitespaceAfter;

    // Text of the window as it is requested.
    QString source() const;
    // Lines the window holds, duplicates collapsed.
    int lineCount() const { return lines.size(); }
};

// Splits a document into translation windows and rebuilds it from the translated
// lines. Only the texts are requested; the whitespace comes back as written.
class DocumentSegmenter
{
public:
    // Splits \p document into windows of whole lines, folding a line that repeats
    // the text of the one before it. A window fills up to \p wordLimit words and
    // \p lineLimit lines; a line over the word limit fills a window on its own.
    // unlimitedSentinel leaves the line count open, and a document carrying no text
    // yields no window.
    static QVector<DocumentWindow> partition(const QString& document,
                                             int wordLimit = Defaults::documentWindowWords,
                                             int lineLimit = Defaults::documentWindowLines);
    // Answers, one per window line, keyed by the line number 1..lineCount();
    // nothing when the response does not hold those lines, each once and with text.
    static std::optional<QStringList> splitTranslation(const DocumentWindow& window,
                                                       const QString& response);
    // One window as it reads in the document, whitespace included. A window without
    // a full answer keeps its source text.
    static QString renderWindow(const DocumentWindow& window, const QStringList& translatedLines);
    // Rebuilds the document from one line list per window, as it was written.
    static QString assemble(const QVector<DocumentWindow>& windows,
                            const QVector<QStringList>& translations);
};
