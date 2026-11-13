#pragma once

#include <QDialog>
#include <QVector>

#include "core/models/Glossary.h"

class QComboBox;
class QTextEdit;
class TranslationEdit;

// Renders the prompt the current templates produce for a sample request. The
// sampled options start from the persisted configuration and are only
// overridden inside this dialog to drive the preview.
class PromptPreviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PromptPreviewDialog(QWidget* parent = nullptr);

private slots:
    void refresh();

private:
    QTextEdit* createTextBox();
    TranslationEdit* createPreviewPane();

    bool m_glossaryEnabled = false;
    QVector<GlossaryEntry> m_glossary;
    QTextEdit* m_source = nullptr;
    QComboBox* m_sourceLang = nullptr;
    QComboBox* m_target = nullptr;
    QComboBox* m_tone = nullptr;
    QTextEdit* m_style = nullptr;
    QTextEdit* m_background = nullptr;
    TranslationEdit* m_system = nullptr;
    TranslationEdit* m_user = nullptr;
};
