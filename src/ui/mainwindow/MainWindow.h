#pragma once

#include <QMainWindow>

#include "core/history/HistoryManager.h"
#include "core/translation/TranslationEngine.h"
#include "ui/widgets/TranslationEdit.h"

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

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    void retranslateUi();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onSourceChanged();
    void translateNow();
    void swapLanguages();
    void pasteSource();
    void undoResult();
    void redoResult();
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
    void updateSwapButtonGeometry();

    void populateLanguageCombos();
    void populateToneCombo();
    void syncLanguageMenu();
    void applyAlwaysOnTop(bool onTop);
    void applyEditorFonts();
    void applyClipboardMonitoring(bool enabled);

    TranslationContext currentContext() const;
    void setBusy(bool busy);
    void setStatusMessage(const QString& message, bool isError);
    void translateClipboard();

    void pushResultSnapshot();
    void updateUndoRedoActions();

    void restoreGeometryFromConfig();
    void saveGeometryToConfig();

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
    QAction* m_clipboardAction = nullptr;
    QAction* m_documentAction = nullptr;
    QAction* m_exportAction = nullptr;
    QAction* m_historyAction = nullptr;
    QAction* m_settingsAction = nullptr;
    QAction* m_glossaryAction = nullptr;
    QAction* m_toneAction = nullptr;
    QAction* m_swapAction = nullptr;
    QAction* m_clearAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_clearResultAction = nullptr;
    QAction* m_copyAction = nullptr;
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
    QSystemTrayIcon* m_tray = nullptr;
    CandidatePopup* m_popup = nullptr;
    QString m_lastClipboard;
    QStringList m_resultSnapshots;
    QStringList m_redoSnapshots;
};
