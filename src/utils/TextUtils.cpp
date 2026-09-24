#include "TextUtils.h"

#include <limits>

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

}

namespace TextUtils {

WordSpan wordSpanAt(const QString& text, int position)
{
    if (text.isEmpty())
        return {};

    position = qBound(0, position, text.size());
    if (position < text.size() && text.at(position).isSpace())
        return {};

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

int nearestOccurrence(const QString& text, const QString& needle, int anchor)
{
    if (needle.isEmpty())
        return -1;

    anchor = qBound(0, anchor, text.size());
    int bestStart = -1;
    int bestDistance = std::numeric_limits<int>::max();
    int index = text.indexOf(needle);
    while (index != -1) {
        const int distance = qAbs(index - anchor);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestStart = index;
        }
        index = text.indexOf(needle, index + 1);
    }
    return bestStart;
}

}
