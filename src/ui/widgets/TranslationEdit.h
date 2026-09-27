#pragma once

#include <QTextEdit>

class TranslationEdit : public QTextEdit
{
    Q_OBJECT

public:
    explicit TranslationEdit(QWidget* parent = nullptr);

    void setResult(const QString& text);
    QString result() const;
    // Replaces \p targetText when it still starts at \p start, so a replacement
    // is rejected outright once the translation has moved on.
    bool replaceWordAt(int start, const QString& targetText, const QString& replacement);

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
};
