#include "TextUtils.h"

#include <QList>
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

// Word boundaries of \p text under the Unicode word break rules; a boundary is
// also emitted at the end of the text when the finder stops short of it.
QList<int> wordBoundaries(const QString& text)
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
    return boundaries;
}

// Index of the last boundary at or before \p position.
int snapBefore(const QList<int>& boundaries, int position)
{
    int result = boundaries.first();
    for (int boundary : boundaries) {
        if (boundary > position)
            break;
        result = boundary;
    }
    return result;
}

// Index of the first boundary at or after \p position.
int snapAfter(const QList<int>& boundaries, int position)
{
    for (int boundary : boundaries) {
        if (boundary >= position)
            return boundary;
    }
    return boundaries.last();
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

    const QList<int> boundaries = wordBoundaries(text);

    for (int attempt : {position, position - 1}) {
        if (attempt < 0 || attempt >= text.size())
            continue;
        if (text.at(attempt).isSpace())
            continue;

        int segmentIndex = 0;
        for (int i = 0; i < boundaries.size() - 1; ++i) {
            if (boundaries.at(i) <= attempt && attempt < boundaries.at(i + 1)) {
                segmentIndex = i;
                break;
            }
        }

        int start = boundaries.at(segmentIndex);
        int end = boundaries.at(segmentIndex + 1);
        while (start < end && text.at(start).isSpace())
            ++start;
        while (end > start && text.at(end - 1).isSpace())
            --end;
        if (end <= start)
            continue;

        const int cjkCount = countCjkCodePoints(text, start, end);
        const bool cjkDominant = cjkCount * 2 >= (end - start);
        if (cjkDominant && (end - start) > kMaxCjkRunLength)
            continue;

        return {start, end};
    }
    return {};
}

// Sentence holding \p position, delimited by the Unicode sentence break rules.
WordSpan sentenceSpanAt(const QString& text, int position)
{
    if (text.isEmpty())
        return {};

    position = qBound(0, position, text.size());
    QTextBoundaryFinder finder(QTextBoundaryFinder::Sentence, text);
    finder.setPosition(position);

    int start = finder.toPreviousBoundary();
    if (start == -1)
        start = 0;
    // toPreviousBoundary lands on the end of the preceding sentence when the
    // position sits exactly on a boundary, so walk forward again until the
    // returned run contains the position.
    while (start > position) {
        const int previous = finder.toPreviousBoundary();
        if (previous == -1)
            break;
        start = previous;
    }

    finder.setPosition(position);
    int end = finder.toNextBoundary();
    if (end == -1)
        end = text.size();

    while (start < end && text.at(start).isSpace())
        ++start;
    while (end > start && text.at(end - 1).isSpace())
        --end;
    if (end <= start)
        return {};
    return {start, end};
}

Fragment candidateFragment(const QString& text, int selectionStart, int selectionEnd,
                           int contextChars, bool sentenceScoped)
{
    if (text.isEmpty())
        return {};

    selectionStart = qBound(0, selectionStart, text.size());
    selectionEnd = qBound(selectionStart, selectionEnd, text.size());
    if (selectionEnd <= selectionStart)
        return {};

    int start = selectionStart;
    int end = selectionEnd;
    if (sentenceScoped) {
        const WordSpan sentence = sentenceSpanAt(text, selectionStart);
        if (sentence.valid()) {
            start = sentence.start;
            end = sentence.end;
        }
    }

    if (contextChars > 0) {
        const QList<int> boundaries = wordBoundaries(text);
        start = snapAfter(boundaries, qMax(0, selectionStart - contextChars));
        end = snapBefore(boundaries, qMin(text.size(), selectionEnd + contextChars));
        // A boundary snap may overshoot the selection; keep the mark intact.
        start = qMin(start, selectionStart);
        end = qMax(end, selectionEnd);
    }

    Fragment fragment;
    fragment.text = text.mid(start, end - start);
    fragment.sourceOffset = start;
    fragment.markStart = selectionStart - start;
    fragment.markEnd = selectionEnd - start;
    return fragment;
}

WordSpan resolveCandidate(const QString& text, int selectionStart, int selectionEnd,
                          const QString& candidate)
{
    if (candidate.isEmpty())
        return {};
    selectionStart = qBound(0, selectionStart, text.size());
    selectionEnd = qBound(selectionStart, selectionEnd, text.size());

    // Only an occurrence that covers the whole selection is a candidate, and it
    // has to be the only one, otherwise the fragment is ambiguous.
    const auto covering = [&](const QString& haystack, const QString& needle) {
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
    };

    const int length = candidate.size();
    const QList<int> starts = covering(text, candidate);
    if (starts.size() == 1)
        return {starts.first(), starts.first() + length};
    if (!starts.isEmpty())
        return {};

    // A model that drops a sentence-initial capital still resolves, as long as
    // the relaxed match stays unambiguous.
    const QList<int> folded = covering(text.toCaseFolded(), candidate.toCaseFolded());
    if (folded.size() == 1)
        return {folded.first(), folded.first() + length};
    return {};
}

}
