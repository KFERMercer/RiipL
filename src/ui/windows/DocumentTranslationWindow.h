#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

#include "core/document/DocumentSegmenter.h"
#include "core/translation/DocumentTranslator.h"
#include "core/translation/PromptBuilder.h"

class QAction;
class QGroupBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class TranslationEdit;

// A top-level peer of the main window. Closing hides it with the document, the
// progress and the result kept, so a run carries on in the background.
class DocumentTranslationWindow : public QWidget
{
    Q_OBJECT

public:
    // Read when a run starts, so the window always translates with the current
    // settings.
    using ContextProvider = std::function<TranslationContext()>;

    explicit DocumentTranslationWindow(ContextProvider context);

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void browse();
    void start();
    void stop();
    void exportResult();

private:
    // Reported status, held as a state so a language change can render it again.
    enum class Status {
        Ready,
        ChooseFile,
        CannotOpen,
        NoContent,
        Translating,
        Progress,
        Finished,
        Failed,
        Stopped,
        Exported,
        CannotWrite,
    };

    // Reads the document the path box holds; false when there is nothing to
    // translate, with the reason already reported.
    bool loadFile();
    // Drops the result on screen without touching the loaded document.
    void clearResult();
    void setRunning(bool running);
    void endRun();
    void setStatus(Status status, const QString& argument = QString());
    QString statusText() const;
    void retranslateUi();

    ContextProvider m_contextProvider;
    DocumentTranslator m_translator;
    QVector<DocumentWindow> m_windows;
    // Path the loaded windows came from; the export dialog suggests its name.
    QString m_loadedPath;
    // Document as it stands, sent once a window has been accepted; the windows the
    // run has not reached yet carry their source text.
    QString m_completedText;
    Status m_status = Status::Ready;
    QString m_statusArgument;
    QLineEdit* m_pathEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_statusLabel = nullptr;
    TranslationEdit* m_preview = nullptr;
    QGroupBox* m_previewGroup = nullptr;
    QAction* m_translateAction = nullptr;
    QAction* m_stopAction = nullptr;
    QPushButton* m_exportButton = nullptr;
    QPushButton* m_closeButton = nullptr;
    bool m_running = false;
};
