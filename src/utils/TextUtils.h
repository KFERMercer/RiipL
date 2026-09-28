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
    // Offset of the slice inside the text it was taken from, so an occurrence
    // found in the slice maps back to an absolute position.
    int sourceOffset = 0;
    int markStart = -1;
    int markEnd = -1;

    bool valid() const { return markStart >= 0 && markEnd > markStart; }
};

WordSpan wordSpanAt(const QString& text, int position);

// Context window handed to the candidate wording prompt: the sentence holding
// the selection, widened by up to \p contextChars on either side. Both edges
// snap to the word boundaries the editor uses, so a fragment never starts or
// ends inside a word, and \p contextChars of zero keeps the window inside one
// sentence.
Fragment candidateFragment(const QString& text, int selectionStart, int selectionEnd,
                           int contextChars, bool sentenceScoped = true);

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
