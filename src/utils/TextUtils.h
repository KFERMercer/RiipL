#pragma once

#include <QString>
#include <QStringList>

namespace TextUtils {

struct WordSpan
{
    int start = -1;
    int end = -1;

    bool valid() const { return start >= 0 && end > start; }
    int length() const { return end - start; }
};

// A slice of a longer text together with the offsets of the selection inside it.
struct Fragment
{
    QString text;
    int markStart = -1;
    int markEnd = -1;

    bool valid() const { return markStart >= 0 && markEnd > markStart; }
};

// Window of \p before words in front of a selection and \p after words behind it.
struct WordWindow
{
    int before = 0;
    int after = 0;
};

WordSpan wordSpanAt(const QString& text, int position);

// Words of \p text around the selection, both edges snapped to a word so a
// fragment never starts or ends inside one. A window wide enough to hold the
// whole text yields the whole text, which is how a short text is told apart from
// a local window.
Fragment candidateFragment(const QString& text, int selectionStart, int selectionEnd,
                           WordWindow window);

// Span of \p text that the chosen \p replacement overwrites when the model
// proposed it for \p target. The target is matched verbatim first and
// case-insensitively only as a fallback, so a dropped sentence-initial capital
// still resolves. The match is then widened over characters of \p text that some
// \p replacement restates at its own edge, which keeps a replacement whose text
// repeats the characters outside the target from doubling them in the result.
// An invalid span means no occurrence covers the selection, or several do.
WordSpan replacementSpan(const QString& text, int selectionStart, int selectionEnd,
                         const QString& target, const QStringList& replacements);

}
