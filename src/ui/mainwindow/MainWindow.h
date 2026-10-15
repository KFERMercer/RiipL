#pragma once

#include <QMainWindow>
#include <QString>
#include <QUndoStack>

#include <memory>

#include "core/history/HistoryManager.h"
#include "core/network/ApiClient.h"
#include "core/translation/PromptBuilder.h"
#include "core/translation/TranslationActivity.h"
#include "core/translation/TranslationEngine.h"
#include "core/translation/YoloTranslator.h"
#include "ui/widgets/AppIcons.h"
#include "ui/widgets/SessionClipboard.h"
#include "ui/widgets/TranslationEdit.h"

class QActionGroup;
class QComboBox;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QResizeEvent;
class QSplitter;
class QSystemTrayIcon;
class QTimer;
class QToolButton;
class CandidatePopup;
class DocumentTranslationWindow;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void retranslateUi();

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onSourceChanged();
    void translateNow();
    void swapLanguages();
    void pasteSource();
    void copyResult();
    void exportTranslation();
    void showSettingsDialog();
    void showHistoryDialog();
    void showDocumentWindow();
    void onConfigChanged(const QString& key);
    void toggleVisible();

private:
    // Held as a state rather than a snapshot of translated text, so a language
    // change can re-render the status bar.
    enum class Status {
        Ready,
        Translating,
        Finished,
        Cancelled,
        ReplacementSkipped,
        Restored,
        Reapplied,
        Copied,
        EmptySource,
        NothingToExport,
        Exported,
        CannotWrite,
        Failure,
    };

    void setStatus(Status status, const QString& argument = QString());
    void setStatusFailure(const ApiClient::Error& failure);
    QString statusText() const;
    bool statusIsError() const;
    void updateCountLabel();

    QWidget* createLeftPane();
    QWidget* createRightPane();
    void buildMenus();
    void buildTray();
    void updateSwapButtonGeometry();
    void closeDocumentWindow();
    void quitApplication();

    void populateLanguageCombos();
    void populateToneCombo();
    void syncLanguageMenu();
    void rebuildApiPresetMenu();
    void syncApiPresetMenu();
    void applyAlwaysOnTop(bool onTop);
    void applyEditorFonts();
    void applyClipboardWatching();
    // Switches YOLO mode for this run of the application; it is not a stored
    // setting.
    void applyYoloMode(bool enabled);

    TranslationContext currentContext() const;
    void setBusy(bool busy);
    void translateClipboard();
    void takeClipboardText();
    void onClipboardTimeout();
    void writeClipboard(const QString& text);
    bool clipboardTextIsNewWork(const QString& text) const;
    void stopTranslations();
    void stopAllTranslations();
    void updateStopAction();
    void updateTrayIcon();

    // The pane grows while a translation streams; closing the run turns it into
    // one undo entry.
    void appendStreamedResult(const QString& piece);
    void endResultStream();
    void discardResultStream();
    void setResultText(const QString& text);

    QSplitter* m_splitter = nullptr;
    QComboBox* m_sourceLang = nullptr;
    QComboBox* m_targetLang = nullptr;
    QComboBox* m_tone = nullptr;
    QPlainTextEdit* m_sourceEdit = nullptr;
    TranslationEdit* m_resultEdit = nullptr;
    QLabel* m_countLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QToolButton* m_swapButton = nullptr;
    QToolButton* m_translateButton = nullptr;

    QAction* m_translateAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_autoTranslateAction = nullptr;
    QAction* m_onTopAction = nullptr;
    QAction* m_notificationsAction = nullptr;
    QAction* m_clipboardAction = nullptr;
    QAction* m_yoloAction = nullptr;
    QAction* m_documentAction = nullptr;
    QAction* m_exportAction = nullptr;
    QAction* m_historyAction = nullptr;
    QAction* m_settingsAction = nullptr;
    QAction* m_glossaryAction = nullptr;
    QAction* m_toneAction = nullptr;
    QAction* m_apiPresetAction = nullptr;
    QAction* m_swapAction = nullptr;
    QAction* m_clearAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_clearResultAction = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_exitAction = nullptr;
    QAction* m_aboutAction = nullptr;
    QAction* m_trayExitAction = nullptr;
    QAction* m_trayShowHideAction = nullptr;
    QAction* m_trayTranslateClipAction = nullptr;
    QAction* m_trayDocumentAction = nullptr;
    QAction* m_trayStopAction = nullptr;

    QMenu* m_fileMenu = nullptr;
    QMenu* m_editMenu = nullptr;
    QMenu* m_viewMenu = nullptr;
    QMenu* m_toolsMenu = nullptr;
    QMenu* m_helpMenu = nullptr;
    QMenu* m_languageMenu = nullptr;
    QMenu* m_apiPresetMenu = nullptr;
    QActionGroup* m_apiPresetGroup = nullptr;
    QAction* m_apiPresetPlaceholder = nullptr;

    QTimer* m_debounce = nullptr;
    QTimer* m_clipboardTimer = nullptr;
    // Parentless, so it is a peer of this window rather than a transient on top.
    std::unique_ptr<DocumentTranslationWindow> m_documentWindow;
    TranslationEngine m_engine;
    YoloTranslator m_yolo;
    // Declared in teardown order: the watcher goes before the translators whose
    // cancellation still moves the shared pool.
    TranslationActivity m_activity;
    HistoryManager m_history;
    QSystemTrayIcon* m_tray = nullptr;
    // Fixed for the session, so every tray icon comes from the same family.
    TrayIcons::Family m_trayFamily = TrayIcons::Family::App;
    CandidatePopup* m_popup = nullptr;
    Status m_status = Status::Ready;
    QString m_statusArgument;
    ApiClient::Error m_statusFailure;
    // Text this application last wrote, without the blanks around it; a clipboard
    // change carrying it is not new work.
    QString m_clipboardWritten;
    SessionClipboard m_clipboard;
    bool m_clipboardReadPending = false;
    // Translation the popup's replacement spans were resolved against.
    QString m_candidateOrigin;
    QUndoStack m_resultHistory;
    QString m_streamOrigin;
    bool m_streamOpen = false;
};
