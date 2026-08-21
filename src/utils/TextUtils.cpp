#include "TextUtils.h"

#include <QList>
#include <QTextBoundaryFinder>

namespace {

bool isCjkChar(const QChar& ch)
{
    const char32_t code = ch.unicode();
    return (code >= 0x2E80 && code <= 0x9FFF)
        || (code >= 0x3400 && code <= 0x4DBF)
        || (code >= 0xF900 && code <= 0xFAFF)
        || (code >= 0x20000 && code <= 0x3FFFF)
        || (code >= 0x3040 && code <= 0x30FF)
        || (code >= 0xAC00 && code <= 0xD7AF);
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

        int cjkCount = 0;
        for (int i = start; i < end; ++i) {
            if (isCjkChar(text.at(i)))
                ++cjkCount;
        }
        const bool cjkDominant = cjkCount * 2 >= (end - start);
        if (cjkDominant && (end - start) > kMaxCjkRunLength)
            continue;

        return {start, end};
    }
    return {};
}

}
