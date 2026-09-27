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

// Span of the occurrence of \p candidate covering [selectionStart,
// selectionEnd). The candidate is matched verbatim first and case-insensitively
// only as a fallback, so a sentence-initial capital the model dropped still
// resolves. Returns an invalid span when no occurrence covers the selection or
// when several do, which keeps a repeated fragment from being replaced blindly.
WordSpan resolveCandidate(const QString& text, int selectionStart, int selectionEnd,
                          const QString& candidate);

}
