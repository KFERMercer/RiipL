#include "TranslationEdit.h"

#include "utils/TextUtils.h"

#include <limits>

#include <QAbstractTextDocumentLayout>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextDocument>

TranslationEdit::TranslationEdit(QWidget* parent)
    : QTextEdit(parent)
{
    setReadOnly(true);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
}

void TranslationEdit::setResult(const QString& text)
{
    clearHighlight();
    setPlainText(text);
}

QString TranslationEdit::result() const
{
    return toPlainText();
}

void TranslationEdit::clearHighlight()
{
    setExtraSelections({});
    m_wordCursor = QTextCursor();
}

QTextCursor TranslationEdit::locateText(const QTextCursor& hint, const QString& text) const
{
    if (text.isEmpty())
        return QTextCursor();
    if (hint.hasSelection() && hint.selectedText() == text)
        return hint;

    const QString docText = toPlainText();
    int anchor = hint.hasSelection() ? hint.selectionStart() : hint.position();
    anchor = qBound(0, anchor, docText.size());

    int bestStart = -1;
    int bestDistance = std::numeric_limits<int>::max();
    int index = docText.indexOf(text);
    while (index != -1) {
        const int distance = qAbs(index - anchor);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestStart = index;
        }
        index = docText.indexOf(text, index + 1);
    }
    if (bestStart == -1)
        return QTextCursor();

    QTextCursor cursor(document());
    cursor.setPosition(bestStart);
    cursor.setPosition(bestStart + text.size(), QTextCursor::KeepAnchor);
    return cursor;
}

void TranslationEdit::mousePressEvent(QMouseEvent* event)
{
    m_pressValid = false;
    m_pressPos = event->pos();
    if (event->button() == Qt::LeftButton) {
        const QTextCursor hitCursor = cursorForPosition(event->pos());
        const int position = hitCursor.position();
        // Fuzzy hits snap blank clicks (below or past the text) to the end of
        // the document, so require an exact hit on an actual character.
        const QPointF docPos(event->pos().x() + horizontalScrollBar()->value(),
                             event->pos().y() + verticalScrollBar()->value());
        const int exactPosition = document()->documentLayout()->hitTest(
            docPos, Qt::ExactHit);
        const bool onCharacter = exactPosition != -1;

        int charPosition = -1;
        if (onCharacter) {
            // The insertion point lands before the clicked character on
            // left-half clicks only; pick the character under the point.
            const qreal boundaryX = cursorRect(hitCursor).center().x();
            const bool beforeBoundary = event->pos().x() < boundaryX;
            charPosition = beforeBoundary ? position - 1 : position;
        }
        const TextUtils::WordSpan span = onCharacter && charPosition >= 0
            ? TextUtils::wordSpanAt(toPlainText(), charPosition)
            : TextUtils::WordSpan{};
        if (span.valid()) {
            QTextCursor cursor(document());
            cursor.setPosition(span.start);
            cursor.setPosition(span.end, QTextCursor::KeepAnchor);
            m_wordCursor = cursor;

            QTextEdit::ExtraSelection selection;
            selection.cursor = cursor;
            QPalette pal = palette();
            selection.format.setBackground(pal.color(QPalette::Window));
            selection.format.setForeground(pal.color(QPalette::Link));
            selection.format.setFontUnderline(true);
            setExtraSelections({selection});
            m_pressValid = true;
        } else {
            clearHighlight();
        }
    }
    QTextEdit::mousePressEvent(event);
}

void TranslationEdit::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_pressValid && event->button() == Qt::LeftButton
        && (event->pos() - m_pressPos).manhattanLength() <= 4) {
        m_pressValid = false;
        const QString word = m_wordCursor.selectedText().trimmed();
        if (!word.isEmpty())
            emit wordRequested(word, event->globalPosition().toPoint(), m_wordCursor);
    } else {
        m_pressValid = false;
    }
    QTextEdit::mouseReleaseEvent(event);
}

bool TranslationEdit::replaceWordAt(const QTextCursor& hint,
                                    const QString& targetText,
                                    const QString& replacement)
{
    if (targetText.isEmpty()) {
        emit replacementSkipped(targetText);
        return false;
    }

    QTextCursor target = locateText(hint, targetText);
    if (!target.hasSelection()) {
        emit replacementSkipped(targetText);
        return false;
    }

    target.insertText(replacement);
    clearHighlight();
    return true;
}
