#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>
#include <memory>

#include "core/document/DocumentSegmenter.h"
#include "core/translation/DocumentTranslator.h"
#include "core/translation/PromptBuilder.h"

class QAction;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class StatusPresenter;
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
    ~DocumentTranslationWindow() override;

    // Ends a run under way; called from outside this window.
    void stopTranslation();

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void browse();
    void start();
    void exportResult();
    void onConfigChanged(const QString& key);

private:
    // Held as a state, so a language change re-renders it.
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
    // Whether the run accepted a window; the progress bar counts them.
    bool hasTranslatedWindow() const;
    void endRun();
    void setStatus(Status status, const QString& argument = QString());
    QString statusText(Status status, const QString& argument) const;
    static bool statusIsError(Status status);
    void retranslateUi();
    void populateApiPresets();
    void syncApiPreset();
    void populateTones();
    void populateLanguageCombo(QComboBox* combo, bool includeAuto, const QString& key);
    void updateArrowIcon();
    void updateOptionsEnabled();

    ContextProvider m_contextProvider;
    DocumentTranslator m_translator;
    QVector<DocumentWindow> m_windows;
    // Path the loaded windows came from; the export dialog suggests its name.
    QString m_loadedPath;
    // Document as the run reported it last, the windows it has not reached carrying
    // their source text.
    QString m_completedText;
    QString m_resultLang;
    QLineEdit* m_pathEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    QComboBox* m_sourceLang = nullptr;
    QLabel* m_arrow = nullptr;
    QComboBox* m_targetLang = nullptr;
    QComboBox* m_tone = nullptr;
    QAction* m_contextAction = nullptr;
    QComboBox* m_apiPreset = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_statusLabel = nullptr;
    std::unique_ptr<StatusPresenter> m_statusPresenter;
    TranslationEdit* m_preview = nullptr;
    QGroupBox* m_previewGroup = nullptr;
    QAction* m_translateAction = nullptr;
    QAction* m_stopAction = nullptr;
    QPushButton* m_exportButton = nullptr;
    QPushButton* m_closeButton = nullptr;
    bool m_running = false;
};
