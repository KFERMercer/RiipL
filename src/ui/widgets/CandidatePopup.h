#pragma once

#include <QTextCursor>
#include <QWidget>

#include "core/translation/TranslationEngine.h"

class QLabel;
class QListWidget;
class TranslationEngine;

class CandidatePopup : public QWidget
{
    Q_OBJECT

public:
    explicit CandidatePopup(TranslationEngine* engine, QWidget* parent = nullptr);

    void openFor(const QString& word,
                 const QPoint& globalPos,
                 const QString& sourceText,
                 const QString& translatedText,
                 const QString& targetLang,
                 const QTextCursor& cursor);

signals:
    void candidateChosen(const QString& replaceTarget,
                         const QString& replacement,
                         const QTextCursor& cursor);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    TranslationEngine* m_engine = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_header = nullptr;
    QLabel* m_status = nullptr;
    QString m_word;
    QString m_replaceTarget;
    QTextCursor m_cursor;
};
