#pragma once

#include <QString>

namespace TextUtils {

struct WordSpan
{
    int start = -1;
    int end = -1;

    bool valid() const { return start >= 0 && end > start; }
    int length() const { return end - start; }
};

WordSpan wordSpanAt(const QString& text, int position);

// Index of the occurrence of \p needle closest to \p anchor, or -1 when
// \p needle is empty or not found.
int nearestOccurrence(const QString& text, const QString& needle, int anchor);

}
