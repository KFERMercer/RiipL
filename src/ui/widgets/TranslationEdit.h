#pragma once

#include <QTextEdit>

class TranslationEdit : public QTextEdit
{
    Q_OBJECT

public:
    explicit TranslationEdit(QWidget* parent = nullptr);

    void setResult(const QString& text);
    QString result() const;
    bool replaceWordAt(const QTextCursor& hint, const QString& targetText, const QString& replacement);

signals:
    void wordRequested(const QString& word, const QPoint& globalPos, const QTextCursor& cursor);
    void replacementSkipped(const QString& targetText);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void clearHighlight();

    QTextCursor m_wordCursor;
    QPoint m_pressPos;
    bool m_pressValid = false;
};
