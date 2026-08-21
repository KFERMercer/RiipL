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

}
