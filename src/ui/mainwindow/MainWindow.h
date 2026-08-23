#pragma once

#include <QMainWindow>

#include "core/history/HistoryManager.h"
#include "core/translation/TranslationEngine.h"
#include "platform/GlobalHotkey.h"
#include "ui/widgets/TranslationEdit.h"

class QComboBox;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QSystemTrayIcon;
class QTimer;
class CandidatePopup;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    void retranslateUi();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onSourceChanged();
    void translateNow();
    void swapLanguages();
    void pasteSource();
    void copyResult();
    void exportTranslation();
    void showSettingsDialog();
    void showHistoryDialog();
    void onConfigChanged(const QString& key);
    void toggleVisible();

private:
    QWidget* createLeftPane();
    QWidget* createRightPane();
    void buildMenus();
    void buildTray();
    void populateLanguageCombos();
    void populateToneCombo();
    TranslationContext currentContext() const;
    void setBusy(bool busy);
    void setStatusMessage(const QString& message, bool isError);
    void applyClipboardMonitoring(bool enabled);
    void applyHotkeyFromConfig();
    void applyAlwaysOnTop(bool onTop);
    void applyEditorFonts();
    void syncLanguageMenu();
    void pushResultSnapshot();
    void undoResult();
    void updateUndoAction();
    void restoreGeometryFromConfig();
    void saveGeometryToConfig();
    void translateClipboard();

    QComboBox* m_sourceLang = nullptr;
    QComboBox* m_targetLang = nullptr;
    QComboBox* m_tone = nullptr;
    QPlainTextEdit* m_sourceEdit = nullptr;
    TranslationEdit* m_resultEdit = nullptr;
    QLabel* m_countLabel = nullptr;
    QLabel* m_statusLabel = nullptr;

    QAction* m_translateAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_autoTranslateAction = nullptr;
    QAction* m_onTopAction = nullptr;
    QAction* m_clipboardAction = nullptr;
    QAction* m_documentAction = nullptr;
    QAction* m_exportAction = nullptr;
    QAction* m_historyAction = nullptr;
    QAction* m_settingsAction = nullptr;
    QAction* m_hotkeySettingsAction = nullptr;
    QAction* m_glossaryAction = nullptr;
    QAction* m_toneAction = nullptr;
    QAction* m_swapAction = nullptr;
    QAction* m_clearAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_exitAction = nullptr;
    QAction* m_aboutAction = nullptr;
    QAction* m_trayShowHideAction = nullptr;
    QAction* m_trayTranslateClipAction = nullptr;

    QMenu* m_fileMenu = nullptr;
    QMenu* m_editMenu = nullptr;
    QMenu* m_viewMenu = nullptr;
    QMenu* m_toolsMenu = nullptr;
    QMenu* m_helpMenu = nullptr;
    QMenu* m_languageMenu = nullptr;
    QMenu* m_trayMenu = nullptr;

    QTimer* m_debounce = nullptr;
    QTimer* m_clipboardTimer = nullptr;
    TranslationEngine m_engine;
    HistoryManager m_history;
    GlobalHotkey m_hotkey;
    QSystemTrayIcon* m_tray = nullptr;
    CandidatePopup* m_popup = nullptr;
    QString m_lastClipboard;
    QStringList m_resultSnapshots;
};
