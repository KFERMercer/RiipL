#pragma once

#include <QList>
#include <QMainWindow>
#include <QString>
#include <QUndoStack>

#include <memory>
#include <optional>

#include "core/history/HistoryManager.h"
#include "core/network/ApiClient.h"
#include "core/translation/PromptBuilder.h"
#include "core/translation/TranslationActivity.h"
#include "core/translation/TranslationEngine.h"
#include "core/translation/YoloTranslator.h"
#include "ui/widgets/AppIcons.h"
#include "ui/widgets/SessionClipboard.h"

class QActionGroup;
class QComboBox;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QResizeEvent;
class QSplitter;
class QSystemTrayIcon;
class QTimer;
class QToolBar;
class QToolButton;
class CandidatePopup;
class DocumentTranslationWindow;
class StatusPresenter;
class TranslationEdit;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

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
    // Held as a state, so a language change re-renders the status text.
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
    };

    void setStatus(Status status, const QString& argument = QString());
    // Takes the Error itself, so its text is rendered again on a language change.
    void setStatusFailure(const ApiClient::Error& failure);
    QString statusText(Status status, const QString& argument) const;
    static bool statusIsError(Status status);
    void updateCountLabel();

    QWidget* createLeftPane();
    QWidget* createRightPane();
    void buildMenus();
    void buildTray();
    void showToolBarPopup(const QPoint& position);
    void setToolBarButtonVisible(QAction* button, bool visible);
    void updateSwapButtonMetrics();
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
    // Session-only; YOLO mode is not a stored setting.
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
    void retranslateUi();

    // A streaming run grows the pane and turns into one undo entry when it closes.
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
    std::unique_ptr<StatusPresenter> m_statusPresenter;
    QToolButton* m_swapButton = nullptr;
    QToolButton* m_translateButton = nullptr;
    QToolBar* m_toolBar = nullptr;

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
    QAction* m_settingsSeparator = nullptr;
    // Toolbar buttons between the separators, in the order they appear; the
    // toolbar context menu toggles these.
    QList<QAction*> m_optionalToolBarButtons;
    QAction* m_glossaryAction = nullptr;
    QAction* m_toneAction = nullptr;
    QAction* m_apiPresetAction = nullptr;
    QAction* m_contextPresetAction = nullptr;
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
    // Parentless, so it is a top-level peer of this window.
    std::unique_ptr<DocumentTranslationWindow> m_documentWindow;
    TranslationEngine m_engine;
    // Holds a pointer to the engine, so it is declared after it and destroyed first.
    std::unique_ptr<CandidatePopup> m_popup;
    YoloTranslator m_yolo;
    TranslationActivity m_activity;
    HistoryManager m_history;
    QSystemTrayIcon* m_tray = nullptr;
    // Fixed for the session, so every tray icon comes from the same family.
    TrayIcons::Family m_trayFamily = TrayIcons::Family::App;
    // Last text this application wrote; a clipboard change carrying it is not new work.
    QString m_clipboardWritten;
    SessionClipboard m_clipboard;
    bool m_clipboardReadPending = false;
    // Translation the popup's replacement spans were resolved against.
    QString m_candidateOrigin;
    QUndoStack m_resultHistory;
    QString m_streamOrigin;
    bool m_streamOpen = false;
    // Record of the request in flight, filled from the context it was sent with.
    std::optional<TranslationRecord> m_pendingRecord;
};
