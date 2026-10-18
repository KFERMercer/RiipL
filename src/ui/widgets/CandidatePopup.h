#pragma once

#include <QMenu>
#include <QPoint>
#include <QString>

#include "core/translation/PromptBuilder.h"
#include "core/translation/TranslationEngine.h"

class CandidatePopup : public QMenu
{
    Q_OBJECT

public:
    explicit CandidatePopup(TranslationEngine* engine, QWidget* parent = nullptr);

    void openFor(const QString& word,
                 int selectionStart,
                 int selectionEnd,
                 const QPoint& globalPos,
                 const TranslationContext& context);

signals:
    // \p start and \p length locate the text the replacement overwrites in the
    // translation the popup was opened for.
    void candidateChosen(int start, int length, const QString& replacement);

private:
    void resetEntries();
    void showStatus(const QString& text);
    void showCandidates(const QVector<TranslationEngine::CandidateGroup>& groups);
    void reposition();

    TranslationEngine* m_engine = nullptr;
    QPoint m_anchor;
    QString m_word;
};
