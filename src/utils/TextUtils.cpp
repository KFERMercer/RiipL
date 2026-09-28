#include "TextUtils.h"

#include <QList>
#include <algorithm>
#include <QTextBoundaryFinder>

namespace {

bool isCjkCodePoint(char32_t code)
{
    switch (QChar::script(code)) {
    case QChar::Script_Han:
    case QChar::Script_Hiragana:
    case QChar::Script_Katakana:
    case QChar::Script_Hangul:
        return true;
    default:
        return false;
    }
}

// Counts CJK code points in [from, to), decoding surrogate pairs so
// supplementary-plane ideographs are classified correctly.
int countCjkCodePoints(const QString& text, int from, int to)
{
    int count = 0;
    for (int i = from; i < to; ++i) {
        char32_t code = text.at(i).unicode();
        if (QChar::isHighSurrogate(code) && i + 1 < to
            && QChar::isLowSurrogate(text.at(i + 1).unicode())) {
            code = QChar::surrogateToUcs4(code, text.at(++i).unicode());
        }
        if (isCjkCodePoint(code))
            ++count;
    }
    return count;
}

constexpr int kMaxCjkRunLength = 8;

// Longest run of characters absorbed from each side of a resolved target.
constexpr int kMaxAbsorbedChars = 4;

// Segments of \p text under the Unicode word break rules; a boundary is also
// emitted at the end of the text when the finder stops short of it.
QList<TextUtils::WordSpan> wordSegments(const QString& text)
{
    QTextBoundaryFinder finder(QTextBoundaryFinder::Word, text);
    QList<int> boundaries;
    finder.toStart();
    boundaries.append(finder.position());
    int next = finder.toNextBoundary();
    while (next != -1) {
        boundaries.append(next);
        next = finder.toNextBoundary();
    }
    if (boundaries.last() != text.size())
        boundaries.append(text.size());

    QList<TextUtils::WordSpan> segments;
    segments.reserve(boundaries.size());
    for (int i = 0; i < boundaries.size() - 1; ++i)
        segments.append({boundaries.at(i), boundaries.at(i + 1)});
    return segments;
}

// Strips leading and trailing blanks, leaving the blank run between two words
// invalid so it never resolves to a word.
TextUtils::WordSpan trimmed(const TextUtils::WordSpan& segment, const QString& text)
{
    int start = segment.start;
    int end = segment.end;
    while (start < end && text.at(start).isSpace())
        ++start;
    while (end > start && text.at(end - 1).isSpace())
        --end;
    return {start, end};
}

// True for a segment \p wordSpanAt hands out as one word, which excludes a
// CJK-dominant run past \p kMaxCjkRunLength.
bool isSelectableWord(const QString& text, const TextUtils::WordSpan& span)
{
    if (!span.valid())
        return false;
    const int cjkCount = countCjkCodePoints(text, span.start, span.end);
    const bool cjkDominant = cjkCount * 2 >= span.length();
    return !cjkDominant || span.length() <= kMaxCjkRunLength;
}

// Segment containing \p position, or an invalid span when no segment does.
TextUtils::WordSpan segmentAt(const QList<TextUtils::WordSpan>& segments, int position)
{
    for (const TextUtils::WordSpan& segment : segments) {
        if (segment.start <= position && position < segment.end)
            return segment;
    }
    return {};
}

// An occurrence is a candidate only when it covers the whole selection, and it
// has to be the only one, otherwise the fragment is ambiguous.
QList<int> coveringStarts(const QString& haystack, const QString& needle,
                          int selectionStart, int selectionEnd)
{
    QList<int> starts;
    if (needle.isEmpty())
        return starts;
    const qsizetype length = needle.size();
    qsizetype at = haystack.indexOf(needle);
    while (at != -1) {
        if (at <= selectionStart && at + length >= selectionEnd)
            starts.append(static_cast<int>(at));
        at = haystack.indexOf(needle, at + 1);
    }
    return starts;
}

// Number of replacements that restate the \p length characters of \p text
// ending at \p edge, read from their left end when \p left holds and from their
// right end otherwise. A replacement no longer than the run cannot restate it.
int restatedVotes(const QString& text, const QStringList& replacements,
                  int edge, int length, bool left)
{
    const QString run = text.mid(edge, length);
    int votes = 0;
    for (const QString& replacement : replacements) {
        if (replacement.size() <= length)
            continue;
        if ((left ? replacement.left(length) : replacement.right(length)) == run)
            ++votes;
    }
    return votes;
}

// Widens [start, end) over characters of \p text that a replacement restates at
// its own edge. At most \p maxChars are absorbed per side, only the longest
// restated run of a side is taken, and a run may not reach past whitespace.
TextUtils::WordSpan absorbRestatedEdges(const QString& text, int start, int end,
                                        const QStringList& replacements, int maxChars)
{
    const auto restatedRun = [&](int edge, bool left) {
        for (int length = qMin(maxChars, left ? edge : text.size() - edge); length > 0; --length) {
            const QString run = text.mid(left ? edge - length : edge, length);
            const bool blank = std::any_of(run.cbegin(), run.cend(),
                                           [](QChar ch) { return ch.isSpace(); });
            if (!blank && restatedVotes(text, replacements, left ? edge - length : edge, length, left) > 0)
                return length;
        }
        return 0;
    };
    return {start - restatedRun(start, true), end + restatedRun(end, false)};
}

}

namespace TextUtils {

WordSpan wordSpanAt(const QString& text, int position)
{
    if (text.isEmpty())
        return {};

    position = qBound(0, position, text.size());
    if (position < text.size() && text.at(position).isSpace())
        return {};

    const QList<WordSpan> segments = wordSegments(text);

    for (int attempt : {position, position - 1}) {
        if (attempt < 0 || attempt >= text.size())
            continue;
        if (text.at(attempt).isSpace())
            continue;

        const WordSpan span = trimmed(segmentAt(segments, attempt), text);
        if (!isSelectableWord(text, span))
            continue;

        return span;
    }
    return {};
}

Fragment candidateFragment(const QString& text, int selectionStart, int selectionEnd,
                           WordWindow window)
{
    if (text.isEmpty())
        return {};

    selectionStart = qBound(0, selectionStart, text.size());
    selectionEnd = qBound(selectionStart, selectionEnd, text.size());
    if (selectionEnd <= selectionStart)
        return {};

    // The mark is widened to whole words first, so the words it covers are part
    // of the window rather than something the window reaches past.
    int start = selectionStart;
    int end = selectionEnd;
    const QList<WordSpan> segments = wordSegments(text);
    const WordSpan markedStart = trimmed(segmentAt(segments, selectionStart), text);
    const WordSpan markedEnd = trimmed(segmentAt(segments, qMax(selectionStart, selectionEnd - 1)), text);
    if (markedStart.valid())
        start = markedStart.start;
    if (markedEnd.valid())
        end = markedEnd.end;

    int remaining = window.before;
    int index = 0;
    while (index < segments.size() && segments.at(index).end <= start)
        ++index;
    while (remaining > 0 && index > 0) {
        const WordSpan span = trimmed(segments.at(--index), text);
        if (isSelectableWord(text, span)) {
            start = span.start;
            --remaining;
        }
    }

    remaining = window.after;
    index = 0;
    while (index < segments.size() && segments.at(index).start < end)
        ++index;
    while (remaining > 0 && index < segments.size()) {
        const WordSpan span = trimmed(segments.at(index++), text);
        if (isSelectableWord(text, span)) {
            end = span.end;
            --remaining;
        }
    }

    Fragment fragment;
    fragment.text = text.mid(start, end - start);
    fragment.markStart = selectionStart - start;
    fragment.markEnd = selectionEnd - start;
    return fragment;
}

WordSpan replacementSpan(const QString& text, int selectionStart, int selectionEnd,
                         const QString& target, const QStringList& replacements)
{
    if (target.isEmpty())
        return {};
    selectionStart = qBound(0, selectionStart, text.size());
    selectionEnd = qBound(selectionStart, selectionEnd, text.size());

    const int length = target.size();
    QList<int> starts = coveringStarts(text, target, selectionStart, selectionEnd);
    if (starts.isEmpty()) {
        // A dropped sentence-initial capital still resolves while the match stays
        // unambiguous.
        starts = coveringStarts(text.toCaseFolded(), target.toCaseFolded(),
                                selectionStart, selectionEnd);
    }
    if (starts.size() != 1)
        return {};
    return absorbRestatedEdges(text, starts.first(), starts.first() + length,
                               replacements, kMaxAbsorbedChars);
}

}
