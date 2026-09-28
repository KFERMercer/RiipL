#pragma once

#include <QDialog>
#include <QVector>

#include "core/models/Glossary.h"

class QComboBox;
class QLineEdit;
class QPlainTextEdit;

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
    bool m_glossaryEnabled = false;
    QVector<GlossaryEntry> m_glossary;
    QLineEdit* m_source = nullptr;
    QComboBox* m_sourceLang = nullptr;
    QComboBox* m_target = nullptr;
    QComboBox* m_tone = nullptr;
    QLineEdit* m_style = nullptr;
    QLineEdit* m_background = nullptr;
    QPlainTextEdit* m_output = nullptr;
};
