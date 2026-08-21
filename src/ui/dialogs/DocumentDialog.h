#pragma once

#include <QDialog>

#include "core/translation/PromptBuilder.h"
#include "core/translation/TranslationEngine.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

class DocumentDialog : public QDialog
{
    Q_OBJECT

public:
    DocumentDialog(const TranslationContext& baseContext, QWidget* parent = nullptr);

private slots:
    void browse();
    void start();
    void cancel();
    void exportResult();

private:
    void loadFile();
    void translateNextChunk();
    QStringList buildChunks(const QString& content) const;

    TranslationContext m_baseContext;
    TranslationEngine m_engine;
    QLineEdit* m_pathEdit = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_status = nullptr;
    QPlainTextEdit* m_preview = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_exportButton = nullptr;
    QString m_sourceContent;
    QStringList m_chunks;
    QStringList m_translatedChunks;
    int m_currentChunk = 0;
    bool m_running = false;
};
