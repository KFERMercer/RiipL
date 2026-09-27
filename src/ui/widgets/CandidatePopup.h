#pragma once

#include <QWidget>

#include "core/translation/PromptBuilder.h"
#include "core/translation/TranslationEngine.h"

class QLabel;
class QListWidget;

class CandidatePopup : public QWidget
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
    // \p start is the absolute offset of the resolved fragment in the
    // translation the popup was opened for, and \p target the text there.
    void candidateChosen(int start, const QString& target, const QString& replacement);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    TranslationEngine* m_engine = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_header = nullptr;
    QLabel* m_status = nullptr;
};
