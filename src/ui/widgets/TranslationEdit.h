#pragma once

#include <QTextEdit>

class TranslationEdit : public QTextEdit
{
    Q_OBJECT

public:
    explicit TranslationEdit(QWidget* parent = nullptr);

    // Turns the click-to-look-up behaviour off, so a click in a pane without
    // lookups does not highlight a word.
    void setWordSelectionEnabled(bool enabled);

    void setResult(const QString& text);
    QString result() const;
    // Replaces the \p length characters at \p start, so a replacement is
    // rejected outright once the translation has moved on.
    bool replaceWordAt(int start, int length, const QString& replacement);

signals:
    void wordRequested(const QString& word, int selectionStart, int selectionEnd,
                       const QPoint& globalPos);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void clearHighlight();

    QTextCursor m_wordCursor;
    QPoint m_pressPos;
    bool m_pressValid = false;
    bool m_wordSelection = true;
};
