#pragma once

#include <QFutureWatcher>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <functional>
#include <map>
#include <memory>

#include "core/document/DocumentSegmenter.h"
#include "core/translation/DocumentTranslator.h"
#include "core/translation/PromptBuilder.h"

class QAction;
class QCloseEvent;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class StatusPresenter;
class TranslationEdit;

// A document read off the disk: whole for a preview, split for a run.
struct LoadedDocument
{
    QString path;
    QVector<DocumentWindow> windows;
    // Document as written, for a load that only shows it; a run translates the
    // windows instead.
    QString text;
    // Path the read failed on; empty when the document is usable.
    QString error;
};

// A top-level peer of the main window. Closing stops a run under way or a load it
// started, and hides the window with the document, the progress and the result kept.
class DocumentTranslationWindow : public QWidget
{
    Q_OBJECT

public:
    // Read when a run starts, so the window always translates with the current
    // settings.
    using ContextProvider = std::function<TranslationContext()>;

    explicit DocumentTranslationWindow(ContextProvider context);
    ~DocumentTranslationWindow() override;

    void stopTranslation();

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

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
        Loading,
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

    // Reads the document the path box holds on a worker thread, then translates it
    // while \p thenTranslate holds.
    void loadDocument(bool thenTranslate);
    void handleLoaded();
    void startTranslation();
    void clearResult();
    void setRunning(bool running);
    void setLoading(bool loading);
    void updateActions();
    // Writes the windows reported since the last flush into their places.
    void flushPreview();
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
    QFutureWatcher<LoadedDocument> m_loadWatcher;
    QVector<DocumentWindow> m_windows;
    // Path the loaded windows came from; the export dialog suggests its name.
    QString m_loadedPath;
    // Windows reported but not yet in the preview, by index.
    std::map<int, QString> m_pendingText;
    // Length of every window the preview holds, by index; zero while it holds none.
    QVector<qsizetype> m_placedLength;
    // Highest window the preview holds, -1 while it holds none.
    int m_placedHighest = -1;
    // Document the run reported as finished; a run that ends early is assembled when
    // it is exported.
    QString m_completedText;
    QTimer m_previewTimer;
    // Whether the preview carries the translation rather than the document as it
    // was loaded.
    bool m_streaming = false;
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
    bool m_running = false;
    bool m_loading = false;
    // Whether the load under way starts a run once it lands.
    bool m_startAfterLoad = false;
    // Whether the load under way was dropped, so its document is not wanted.
    bool m_loadDropped = false;
};
