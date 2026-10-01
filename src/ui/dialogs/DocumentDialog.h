#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

#include "core/document/DocumentSegmenter.h"
#include "core/network/ApiClient.h"
#include "core/translation/DocumentTranslator.h"
#include "core/translation/PromptBuilder.h"

class QAction;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class TranslationEdit;

class DocumentDialog : public QDialog
{
    Q_OBJECT

public:
    DocumentDialog(const TranslationContext& baseContext, QWidget* parent = nullptr);

public slots:
    // Asks before abandoning a run, whose translated windows go with the dialog.
    void reject() override;

private slots:
    void browse();
    void start();
    void stop();
    void exportResult();

private:
    // Loads the document the path box holds; false when it could not be read,
    // with the failure already reported.
    bool loadFile();
    void setRunning(bool running);
    void endRun();
    void setStatus(const QString& text, bool error = false);

    TranslationContext m_baseContext;
    DocumentTranslator m_translator;
    QVector<DocumentWindow> m_windows;
    // Path the loaded windows came from, so the box and the document cannot
    // disagree about what Translate translates.
    QString m_loadedPath;
    // Document as it stands, sent once a window has been accepted; the windows the
    // run has not reached yet carry their source text.
    QString m_completedText;
    QLineEdit* m_pathEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_status = nullptr;
    TranslationEdit* m_preview = nullptr;
    QAction* m_translateAction = nullptr;
    QAction* m_stopAction = nullptr;
    QPushButton* m_exportButton = nullptr;
    bool m_running = false;
};
