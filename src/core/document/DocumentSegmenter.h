#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

#include "core/config/Defaults.h"

namespace DocumentWindowLines {

inline const int unlimitedSentinel = 0;

}

// One line of a document: a line equal to the one before it, blank lines aside,
// folds into that entry, and every occurrence keeps the blanks before it.
struct DocumentLine
{
    QString text;
    // One entry per occurrence: the blank lines standing before it, the first
    // occurrence included.
    QVector<int> blanksBefore;
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
    // Splits \p document into windows of whole lines, folding a line equal to
    // the one before it, blank lines aside. A window fills up to \p wordLimit
    // words and \p lineLimit lines; a line over the word limit fills a window on
    // its own, and unlimitedSentinel in \p lineLimit leaves the line count open.
    // A document without a line carrying text yields no window.
    static QVector<DocumentWindow> partition(const QString& document,
                                             int wordLimit = Defaults::documentWindowWords,
                                             int lineLimit = Defaults::documentWindowLines);
    // Answers, one per window line, keyed in the response by the line number
    // 1..lineCount(); nothing when the response does not hold those lines, each
    // once and carrying text.
    static std::optional<QStringList> splitTranslation(const DocumentWindow& window,
                                                       const QString& response);
    // Text of one window as it reads in the document, occurrences and blanks
    // included. A window without a full answer keeps its source text.
    static QString renderWindow(const DocumentWindow& window, const QStringList& translatedLines);
    // Rebuilds the document from one line list per window, as it was written.
    static QString assemble(const QVector<DocumentWindow>& windows,
                            const QVector<QStringList>& translations);
};
