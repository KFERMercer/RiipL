#include "MainWindow.h"

#include "ui/widgets/CandidatePopup.h"
#include "ui/windows/DocumentTranslationWindow.h"
#include "ui/dialogs/ApiPresetDialog.h"
#include "ui/dialogs/ContextPresetDialog.h"
#include "ui/dialogs/GlossaryDialog.h"
#include "ui/dialogs/HistoryDialog.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/dialogs/ToneDialog.h"
#include "ui/widgets/AppFonts.h"
#include "ui/widgets/AppIcons.h"
#include "ui/widgets/StatusPresenter.h"
#include "ui/widgets/ThemeColors.h"
#include "ui/widgets/TranslationEdit.h"
#include "ui/widgets/ConfigEditors.h"
#include "ui/widgets/WindowState.h"
#include "core/config/Preset.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/models/Glossary.h"
#include "core/translation/Language.h"
#include "core/translation/Tone.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QSaveFile>
#include <QSplitter>
#include <QStatusBar>
#include <QStringView>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QWindow>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

constexpr int kMaxResultSteps = 30;

constexpr int kClipboardDelayMs = 500;

constexpr int kAutoTranslateDelayMs = 1000;

constexpr int kNoticeCharacters = 300;

constexpr char kProjectUrl[] = "https://github.com/KFERMercer/RiipL";

class ResultTextCommand : public QUndoCommand
{
public:
    // An applied edit is one the caller has already made, so the redo() that push()
    // triggers must not write it again.
    ResultTextCommand(TranslationEdit* edit, const QString& before, const QString& after, bool applied)
        : m_edit(edit)
        , m_before(before)
        , m_after(after)
        , m_applied(applied)
    {
    }

    void undo() override
    {
        m_edit->setResult(m_before);
        m_applied = false;
    }

    void redo() override
    {
        if (m_applied)
            return;
        m_edit->setResult(m_after);
        m_applied = true;
    }

private:
    TranslationEdit* m_edit;
    QString m_before;
    QString m_after;
    bool m_applied;
};

bool carriesText(const QString& text)
{
    return !QStringView(text).trimmed().isEmpty();
}

QToolButton* makeToolButton(QWidget* parent, QAction* action, Qt::ToolButtonStyle style)
{
    auto* button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setToolButtonStyle(style);
    return button;
}

void bringToFront(QWidget* window)
{
    if (window->isMinimized())
        window->setWindowState(window->windowState() & ~Qt::WindowMinimized);
    window->show();
    window->raise();
    window->activateWindow();
}

}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_engine(this)
    , m_yolo(this)
    , m_activity(this)
    , m_history(ConfigManager::instance()->historyFilePath(), this)
{
    setUnifiedTitleAndToolBarOnMac(true);
    m_resultHistory.setUndoLimit(kMaxResultSteps);

    auto* centralArea = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(centralArea);
    centralLayout->setContentsMargins(0, 0, 0, 0);

    m_splitter = new QSplitter(Qt::Horizontal, centralArea);
    m_splitter->addWidget(createLeftPane());
    m_splitter->addWidget(createRightPane());
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);
    centralLayout->addWidget(m_splitter);
    setCentralWidget(centralArea);

    if (!WindowState::track(this, WindowState::Id::main, m_splitter)) {
        const QRect available = screen()->availableGeometry();
        resize(available.width() * 2 / 5, available.height() / 2);
    }
    // After the stored state, which carries its own value for this.
    m_splitter->setChildrenCollapsible(false);

    m_swapAction = new QAction(this);
    m_swapAction->setIcon(AppIcons::swapHorizontal());
    connect(m_swapAction, &QAction::triggered, this, &MainWindow::swapLanguages);

    m_swapButton = new QToolButton(centralArea);
    m_swapButton->setDefaultAction(m_swapAction);
    m_swapButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_swapButton->raise();
    updateSwapButtonMetrics();
    connect(m_splitter, &QSplitter::splitterMoved, this,
            &MainWindow::updateSwapButtonGeometry);
    QTimer::singleShot(0, this, &MainWindow::updateSwapButtonGeometry);
    setTabOrder(m_sourceLang, m_swapButton);
    setTabOrder(m_swapButton, m_sourceEdit);
    setTabOrder(m_tone, m_targetLang);

    buildMenus();
    buildTray();

    m_statusLabel = new QLabel(this);
    m_statusPresenter = std::make_unique<StatusPresenter>(m_statusLabel);
    statusBar()->addWidget(m_statusLabel, 1);
    setStatus(Status::Ready);

    m_popup = std::make_unique<CandidatePopup>(&m_engine, this);
    connect(m_resultEdit, &TranslationEdit::wordRequested, this,
            [this](const QString& word, int selectionStart, int selectionEnd,
                   const QPoint& globalPos) {
                TranslationContext context = currentContext();
                m_candidateOrigin = m_resultEdit->result();
                context.translatedText = m_candidateOrigin;
                m_popup->openFor(word, selectionStart, selectionEnd, globalPos, context);
            });
    connect(m_popup.get(), &CandidatePopup::candidateChosen, this,
            [this](int start, int length, const QString& replacement) {
                // The spans belong to the translation the popup was opened for.
                if (m_resultEdit->result() != m_candidateOrigin) {
                    setStatus(Status::ReplacementSkipped);
                    return;
                }
                const QString before = m_candidateOrigin;
                if (!m_resultEdit->replaceWordAt(start, length, replacement)) {
                    setStatus(Status::ReplacementSkipped);
                    return;
                }
                m_resultHistory.push(
                    new ResultTextCommand(m_resultEdit, before, m_resultEdit->result(), true));
            });

    connect(&m_engine, &TranslationEngine::partialDelta, this, &MainWindow::appendStreamedResult);
    connect(&m_engine, &TranslationEngine::finished, this, [this](const QString& text) {
        setResultText(text);
        setStatus(Status::Finished);
        if (m_pendingRecord) {
            if (ConfigManager::instance()->boolValue(Keys::historyEnabled)) {
                m_pendingRecord->target = text;
                m_history.addRecord(*m_pendingRecord);
            }
            m_pendingRecord.reset();
        }
    });
    connect(&m_engine, &TranslationEngine::errorOccurred, this, [this](const ApiClient::Error& failure) {
        m_pendingRecord.reset();
        endResultStream();
        setStatusFailure(failure);
    });
    // A candidate lookup also counts as busy, which the status line does not report.
    connect(&m_engine, &TranslationEngine::stateChanged, this, &MainWindow::setBusy);
    connect(&m_engine, &TranslationEngine::stopped, this, [this]() {
        m_pendingRecord.reset();
        endResultStream();
    });

    connect(&m_yolo, &YoloTranslator::translated, this, [this](const QString& text) {
        writeClipboard(text);
        if (m_tray && m_notificationsAction->isChecked()) {
            // The icon overload, so the notice carries the application mark rather
            // than the desktop's stock information icon.
            const QString notice = text.size() > kNoticeCharacters
                ? text.left(kNoticeCharacters) + QStringLiteral("\u2026")
                : text;
            m_tray->showMessage(tr("YOLO"), notice, QApplication::windowIcon());
        }
    });
    connect(&m_engine, &TranslationEngine::stateChanged, this, &MainWindow::updateStopAction);
    connect(&m_yolo, &YoloTranslator::translated, this, &MainWindow::updateStopAction);
    connect(&m_yolo, &YoloTranslator::failed, this, &MainWindow::updateStopAction);
    connect(&m_yolo, &YoloTranslator::stopped, this, &MainWindow::updateStopAction);
    connect(&m_activity, &TranslationActivity::stateChanged, this, &MainWindow::updateTrayIcon);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kAutoTranslateDelayMs);
    connect(m_debounce, &QTimer::timeout, this, &MainWindow::translateNow);

    m_clipboardTimer = new QTimer(this);
    m_clipboardTimer->setSingleShot(true);
    m_clipboardTimer->setInterval(kClipboardDelayMs);
    connect(m_clipboardTimer, &QTimer::timeout, this, &MainWindow::onClipboardTimeout);

    connect(ConfigManager::instance(), &ConfigManager::changed, this, &MainWindow::onConfigChanged);

    populateLanguageCombos();
    populateToneCombo();

    connect(m_sourceLang, &QComboBox::currentIndexChanged, this, [this](int index) {
        ConfigManager::instance()->setValue(Keys::translationSourceLang, m_sourceLang->itemData(index).toString());
    });
    connect(m_targetLang, &QComboBox::currentIndexChanged, this, [this](int index) {
        ConfigManager::instance()->setValue(Keys::translationTargetLang, m_targetLang->itemData(index).toString());
    });
    connect(m_tone, &QComboBox::currentIndexChanged, this, [this](int index) {
        ConfigManager::instance()->setValue(Keys::translationTone, m_tone->itemData(index).toString());
    });

    applyClipboardWatching();
    m_history.setMaxRecords(ConfigManager::instance()->intValue(Keys::historyMaxRecords));

    applyAlwaysOnTop(ConfigManager::instance()->boolValue(Keys::uiAlwaysOnTop));

    applyEditorFonts();

    retranslateUi();
}

MainWindow::~MainWindow() = default;

void MainWindow::closeDocumentWindow()
{
    if (m_documentWindow)
        m_documentWindow->close();
}

void MainWindow::quitApplication()
{
    hide();
    closeDocumentWindow();
    ConfigManager::instance()->flush();
    QCoreApplication::exit(0);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_tray && ConfigManager::instance()->boolValue(Keys::uiMinimizeToTray)) {
        hide();
        event->ignore();
        return;
    }
    closeDocumentWindow();
    ConfigManager::instance()->flush();
    event->accept();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    // Queued, because the panes take their new geometry after this handler returns.
    QTimer::singleShot(0, this, &MainWindow::updateSwapButtonGeometry);
}

void MainWindow::showToolBarPopup(const QPoint& position)
{
    QMenu menu(this);
    menu.addSection(tr("Toolbar Settings"));
    for (QAction* button : m_optionalToolBarButtons) {
        QAction* entry = menu.addAction(button->iconText());
        entry->setCheckable(true);
        entry->setChecked(m_toolBar->actions().contains(button));
        connect(entry, &QAction::toggled, this, [this, button](bool visible) {
            setToolBarButtonVisible(button, visible);
        });
    }
    menu.exec(m_toolBar->mapToGlobal(position));
}

// A button returns ahead of the buttons that follow it in buildMenus() order. The
// action itself cannot be hidden: QAction::setVisible() would take it out of the menus.
void MainWindow::setToolBarButtonVisible(QAction* button, bool visible)
{
    if (!visible) {
        m_toolBar->removeAction(button);
    } else {
        const QList<QAction*> shown = m_toolBar->actions();
        QAction* before = m_settingsSeparator;
        for (int i = m_optionalToolBarButtons.indexOf(button) + 1; i < m_optionalToolBarButtons.size(); ++i) {
            if (shown.contains(m_optionalToolBarButtons.at(i))) {
                before = m_optionalToolBarButtons.at(i);
                break;
            }
        }
        m_toolBar->insertAction(before, button);
    }
    // Keeps the separator pair from standing empty.
    const QList<QAction*> shown = m_toolBar->actions();
    m_settingsSeparator->setVisible(std::any_of(
        m_optionalToolBarButtons.cbegin(), m_optionalToolBarButtons.cend(),
        [&shown](QAction* candidate) { return shown.contains(candidate); }));
}

QWidget* MainWindow::createLeftPane()
{
    auto* pane = new QWidget(this);
    auto* layout = new QVBoxLayout(pane);

    auto* topRow = new QHBoxLayout();
    m_sourceLang = new QComboBox(pane);
    m_sourceLang->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    topRow->addWidget(m_sourceLang);
    topRow->addStretch(1);
    layout->addLayout(topRow);

    m_sourceEdit = new QPlainTextEdit(pane);
    layout->addWidget(m_sourceEdit, 1);

    auto* bottomRow = new QHBoxLayout();
    m_clearAction = new QAction(pane);
    m_pasteAction = new QAction(pane);
    bottomRow->addWidget(makeToolButton(pane, m_pasteAction, Qt::ToolButtonTextOnly));
    bottomRow->addWidget(makeToolButton(pane, m_clearAction, Qt::ToolButtonTextOnly));
    bottomRow->addStretch(1);
    m_countLabel = new QLabel(QStringLiteral("0"), pane);
    bottomRow->addWidget(m_countLabel);
    layout->addLayout(bottomRow);

    connect(m_sourceEdit, &QPlainTextEdit::textChanged, this, &MainWindow::onSourceChanged);
    connect(m_clearAction, &QAction::triggered, this, [this]() {
        m_sourceEdit->clear();
        m_sourceEdit->setFocus();
    });
    connect(m_pasteAction, &QAction::triggered, this, &MainWindow::pasteSource);
    return pane;
}

QWidget* MainWindow::createRightPane()
{
    auto* pane = new QWidget(this);
    auto* layout = new QVBoxLayout(pane);

    auto* topRow = new QHBoxLayout();
    m_targetLang = new QComboBox(pane);
    m_targetLang->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_tone = new QComboBox(pane);
    m_tone->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    topRow->addStretch(1);
    topRow->addWidget(m_tone);
    topRow->addWidget(m_targetLang);
    layout->addLayout(topRow);

    m_resultEdit = new TranslationEdit(pane);
    layout->addWidget(m_resultEdit, 1);

    auto* bottomRow = new QHBoxLayout();
    m_undoAction = new QAction(pane);
    m_undoAction->setIcon(AppIcons::undo());
    m_undoAction->setEnabled(false);
    connect(m_undoAction, &QAction::triggered, this, [this]() {
        discardResultStream();
        m_resultHistory.undo();
        setStatus(Status::Restored);
    });
    m_redoAction = new QAction(pane);
    m_redoAction->setIcon(AppIcons::redo());
    m_redoAction->setEnabled(false);
    connect(m_redoAction, &QAction::triggered, this, [this]() {
        discardResultStream();
        m_resultHistory.redo();
        setStatus(Status::Reapplied);
    });
    // retranslateUi() owns the labels, so the stack only drives the enabled state.
    connect(&m_resultHistory, &QUndoStack::canUndoChanged, m_undoAction, &QAction::setEnabled);
    connect(&m_resultHistory, &QUndoStack::canRedoChanged, m_redoAction, &QAction::setEnabled);
    m_copyAction = new QAction(pane);
    m_clearResultAction = new QAction(pane);
    bottomRow->addWidget(makeToolButton(pane, m_undoAction, Qt::ToolButtonIconOnly));
    bottomRow->addWidget(makeToolButton(pane, m_redoAction, Qt::ToolButtonIconOnly));
    bottomRow->addStretch(1);
    bottomRow->addWidget(makeToolButton(pane, m_clearResultAction, Qt::ToolButtonTextOnly));
    bottomRow->addWidget(makeToolButton(pane, m_copyAction, Qt::ToolButtonTextOnly));
    layout->addLayout(bottomRow);

    connect(m_copyAction, &QAction::triggered, this, &MainWindow::copyResult);
    connect(m_clearResultAction, &QAction::triggered, this, [this]() {
        discardResultStream();
        setResultText(QString());
    });
    return pane;
}

void MainWindow::buildMenus()
{
    m_fileMenu = menuBar()->addMenu(QString());
    m_documentAction = m_fileMenu->addAction(QString());
    m_exportAction = m_fileMenu->addAction(QString());
    m_fileMenu->addSeparator();
    m_translateAction = new QAction(this);
    m_translateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Return")));
    m_fileMenu->addAction(m_translateAction);
    m_fileMenu->addSeparator();
    m_exitAction = m_fileMenu->addAction(QString());
    m_exitAction->setShortcutContext(Qt::ApplicationShortcut);

    m_editMenu = menuBar()->addMenu(QString());
    m_glossaryAction = m_editMenu->addAction(QString());
    m_toneAction = m_editMenu->addAction(QString());
    m_historyAction = m_editMenu->addAction(QString());
    m_apiPresetAction = m_editMenu->addAction(QString());
    m_contextPresetAction = m_editMenu->addAction(QString());

    m_viewMenu = menuBar()->addMenu(QString());
    m_onTopAction = m_viewMenu->addAction(QString());
    m_onTopAction->setCheckable(true);
    m_onTopAction->setChecked(ConfigManager::instance()->boolValue(Keys::uiAlwaysOnTop));
    m_notificationsAction = m_viewMenu->addAction(QString());
    m_notificationsAction->setCheckable(true);
    m_notificationsAction->setChecked(ConfigManager::instance()->boolValue(Keys::uiNotifications));
    m_viewMenu->addSeparator();
    m_languageMenu = m_viewMenu->addMenu(QString());
    auto* languageGroup = new QActionGroup(m_languageMenu);
    languageGroup->setExclusive(true);
    const QList<std::pair<QString, QString>> languageOptions = uiLanguageItems();
    for (const std::pair<QString, QString>& option : languageOptions) {
        QAction* languageAction = m_languageMenu->addAction(option.first);
        languageAction->setData(option.second);
        languageAction->setCheckable(true);
        languageGroup->addAction(languageAction);
        connect(languageAction, &QAction::triggered, this, [this, option]() {
            ConfigManager::instance()->setValue(Keys::uiLanguage, option.second);
        });
    }

    m_toolsMenu = menuBar()->addMenu(QString());
    m_autoTranslateAction = m_toolsMenu->addAction(QString());
    m_autoTranslateAction->setCheckable(true);
    m_autoTranslateAction->setChecked(ConfigManager::instance()->boolValue(Keys::uiAutoTranslate));
    m_clipboardAction = m_toolsMenu->addAction(QString());
    m_clipboardAction->setCheckable(true);
    m_clipboardAction->setChecked(ConfigManager::instance()->boolValue(Keys::clipboardMonitor));
    m_yoloAction = m_toolsMenu->addAction(QString());
    m_yoloAction->setCheckable(true);
    m_toolsMenu->addSeparator();
    m_apiPresetMenu = m_toolsMenu->addMenu(QString());
    m_apiPresetGroup = new QActionGroup(m_apiPresetMenu);
    m_apiPresetGroup->setExclusive(true);
    rebuildApiPresetMenu();
    m_toolsMenu->addSeparator();
    m_settingsAction = m_toolsMenu->addAction(QString());

    m_helpMenu = menuBar()->addMenu(QString());
    m_aboutAction = m_helpMenu->addAction(QString());

    m_toolBar = addToolBar(QString());
    m_toolBar->setMovable(false);
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_toolBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_toolBar, &QWidget::customContextMenuRequested, this, &MainWindow::showToolBarPopup);

    m_translateButton = makeToolButton(m_toolBar, m_translateAction, Qt::ToolButtonTextOnly);
    m_translateButton->setStyleSheet(ThemeColors::primaryButtonStyle());
    m_toolBar->addWidget(m_translateButton);
    connect(m_translateAction, &QAction::triggered, this, &MainWindow::translateNow);

    m_stopAction = m_toolBar->addAction(QString());
    m_stopAction->setEnabled(false);
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::stopTranslations);

    m_toolBar->addSeparator();
    // The buttons between the separators are the ones the toolbar popup toggles.
    m_optionalToolBarButtons = {m_documentAction, m_glossaryAction, m_yoloAction,
                                m_apiPresetAction, m_contextPresetAction, m_historyAction};
    for (QAction* button : m_optionalToolBarButtons)
        m_toolBar->addAction(button);
    m_settingsSeparator = m_toolBar->addSeparator();
    m_toolBar->addAction(m_settingsAction);

    setToolBarButtonVisible(m_glossaryAction, false);
    setToolBarButtonVisible(m_apiPresetAction, false);
    setToolBarButtonVisible(m_contextPresetAction, false);

    connect(m_autoTranslateAction, &QAction::toggled, this, [this](bool checked) {
        ConfigManager::instance()->setValue(Keys::uiAutoTranslate, checked);
    });
    connect(m_onTopAction, &QAction::toggled, this, [this](bool checked) {
        ConfigManager::instance()->setValue(Keys::uiAlwaysOnTop, checked);
    });
    connect(m_notificationsAction, &QAction::toggled, this, [this](bool checked) {
        ConfigManager::instance()->setValue(Keys::uiNotifications, checked);
    });
    connect(m_clipboardAction, &QAction::toggled, this, [this](bool checked) {
        ConfigManager::instance()->setValue(Keys::clipboardMonitor, checked);
    });
    connect(m_yoloAction, &QAction::toggled, this, &MainWindow::applyYoloMode);
    connect(m_documentAction, &QAction::triggered, this, &MainWindow::showDocumentWindow);
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::exportTranslation);
    connect(m_exitAction, &QAction::triggered, this, &MainWindow::quitApplication);
    connect(m_glossaryAction, &QAction::triggered, this, [this]() {
        GlossaryDialog dialog(this);
        dialog.exec();
    });
    connect(m_toneAction, &QAction::triggered, this, [this]() {
        ConfigManager* config = ConfigManager::instance();
        ToneDialog dialog(config->value(Keys::translationCustomTones).toArray(), this);
        if (dialog.exec() == QDialog::Accepted)
            config->setValue(Keys::translationCustomTones, ToneDialog::toJson(dialog.customTones()));
    });
    connect(m_historyAction, &QAction::triggered, this, &MainWindow::showHistoryDialog);
    connect(m_apiPresetAction, &QAction::triggered, this,
            [this]() { ApiPresetDialog::manage(this); });
    connect(m_contextPresetAction, &QAction::triggered, this,
            [this]() { ContextPresetDialog::manage(this); });
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::showSettingsDialog);
    connect(m_aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("About %1").arg(QCoreApplication::applicationName()),
                           tr("<b>%1 %2</b><br/>"
                              "An AI-powered desktop translator.<br/>"
                              "Built with Qt %3.<br/>"
                              "Project homepage: <a href=\"%4\">%4</a>")
                               .arg(QCoreApplication::applicationName(),
                                    QCoreApplication::applicationVersion(),
                                    QString::fromLatin1(QT_VERSION_STR),
                                    QString::fromLatin1(kProjectUrl)));
    });
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_trayFamily = QRandomGenerator::global()->bounded(100) == 0
        ? TrayIcons::Family::Dude
        : TrayIcons::Family::App;
    m_tray = new QSystemTrayIcon(AppIcons::trayIdle(m_trayFamily), this);
    // The tray icon does not take ownership of its context menu, so the window
    // owns it.
    QMenu* menu = new QMenu(this);
    m_trayShowHideAction = menu->addAction(QString());
    menu->addAction(m_notificationsAction);
    menu->addSeparator();
    m_trayTranslateClipAction = menu->addAction(QString());
    m_trayDocumentAction = menu->addAction(QString());
    menu->addAction(m_yoloAction);
    menu->addSeparator();
    menu->addMenu(m_apiPresetMenu);
    menu->addSeparator();
    m_trayStopAction = menu->addAction(QString());
    m_trayExitAction = menu->addAction(QString());
    connect(m_trayExitAction, &QAction::triggered, this, &MainWindow::quitApplication);
    m_tray->setContextMenu(menu);
    m_tray->setToolTip(QCoreApplication::applicationName());
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger)
            toggleVisible();
    });
    connect(m_trayShowHideAction, &QAction::triggered, this, &MainWindow::toggleVisible);
    connect(m_trayTranslateClipAction, &QAction::triggered, this, &MainWindow::translateClipboard);
    connect(m_trayDocumentAction, &QAction::triggered, this, &MainWindow::showDocumentWindow);
    connect(m_trayStopAction, &QAction::triggered, this, &MainWindow::stopAllTranslations);
    m_tray->show();
    updateTrayIcon();
}

void MainWindow::updateSwapButtonMetrics()
{
    // The combo sets the row height, so the floating button matches it exactly.
    const int height = m_sourceLang->sizeHint().height();
    m_swapButton->setFixedSize(height * 2, height);
}

void MainWindow::updateSwapButtonGeometry()
{
    if (!m_splitter || !m_swapButton || !m_sourceLang || !m_tone)
        return;
    QWidget* handle = m_splitter->handle(1);
    QWidget* base = m_swapButton->parentWidget();
    if (!handle || !base)
        return;
    const QPoint comboRow = m_sourceLang->mapTo(
        base, QPoint(0, (m_sourceLang->height() - m_swapButton->height()) / 2));
    const QPoint handleCenter = m_splitter->mapTo(base, handle->geometry().center());
    // Centred on the handle, kept a standard gap clear of the right pane's controls.
    const int gap = std::max(0, style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing));
    const int limit = m_tone->mapTo(base, QPoint(0, 0)).x() - gap - m_swapButton->width();
    m_swapButton->move(std::min(handleCenter.x() - m_swapButton->width() / 2, limit),
                       comboRow.y());
}

void MainWindow::populateLanguageCombos()
{
    const QString source = m_sourceLang ? m_sourceLang->currentData().toString()
                                        : ConfigManager::instance()->stringValue(Keys::translationSourceLang);
    const QString target = m_targetLang ? m_targetLang->currentData().toString()
                                        : ConfigManager::instance()->stringValue(Keys::translationTargetLang);
    if (m_sourceLang) {
        QSignalBlocker blocker(m_sourceLang);
        m_sourceLang->clear();
        for (const LangItem& lang : Languages::all())
            m_sourceLang->addItem(languageLabel(lang.code), lang.code);
        const int index = m_sourceLang->findData(source.isEmpty() ? ConfigManager::instance()->stringValue(Keys::translationSourceLang) : source);
        m_sourceLang->setCurrentIndex(index < 0 ? 0 : index);
    }
    if (m_targetLang) {
        QSignalBlocker blocker(m_targetLang);
        m_targetLang->clear();
        for (const LangItem& lang : Languages::all()) {
            if (lang.code == QLatin1String("auto"))
                continue;
            m_targetLang->addItem(languageLabel(lang.code), lang.code);
        }
        const int index = m_targetLang->findData(target.isEmpty() ? ConfigManager::instance()->stringValue(Keys::translationTargetLang) : target);
        m_targetLang->setCurrentIndex(index < 0 ? 1 : index);
    }
}

void MainWindow::populateToneCombo()
{
    if (!m_tone)
        return;
    QSignalBlocker blocker(m_tone);
    m_tone->clear();
    for (const ToneItem& tone : Tones::presets())
        m_tone->addItem(toneLabel(tone.key), tone.key);
    const QJsonArray custom = ConfigManager::instance()->value(Keys::translationCustomTones).toArray();
    for (const QJsonValue& value : custom) {
        const QJsonObject object = value.toObject();
        m_tone->addItem(object.value(QStringLiteral("name")).toString(), object.value(QStringLiteral("key")).toString());
    }
    const QString wanted = ConfigManager::instance()->stringValue(Keys::translationTone);
    const int index = m_tone->findData(wanted);
    m_tone->setCurrentIndex(index < 0 ? 0 : index);
}

void MainWindow::syncLanguageMenu()
{
    if (!m_languageMenu)
        return;
    const QString current = ConfigManager::instance()->value(Keys::uiLanguage).toString();
    const auto actions = m_languageMenu->actions();
    for (QAction* action : actions) {
        QSignalBlocker blocker(action);
        action->setChecked(action->data().toString() == current);
    }
}

void MainWindow::rebuildApiPresetMenu()
{
    if (!m_apiPresetMenu)
        return;
    m_apiPresetMenu->clear();
    const QVector<Preset> presets =
        Presets::fromJson(ConfigManager::instance()->value(Keys::apiPresets).toArray());
    for (const Preset& preset : presets) {
        QAction* action = m_apiPresetMenu->addAction(preset.name);
        action->setData(preset.name);
        action->setCheckable(true);
        m_apiPresetGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                 [preset]() { Presets::apply(preset, Keys::apiPresetFields()); });
    }
    // An empty submenu opens as a blank popup and reads as a broken control, so
    // a disabled entry explains the state instead.
    if (presets.isEmpty()) {
        m_apiPresetPlaceholder = m_apiPresetMenu->addAction(QString());
        m_apiPresetPlaceholder->setEnabled(false);
    } else {
        m_apiPresetPlaceholder = nullptr;
    }
    syncApiPresetMenu();
}

// Checks the entry whose stored values match the applied configuration; none when
// the fields were edited by hand.
void MainWindow::syncApiPresetMenu()
{
    if (!m_apiPresetMenu)
        return;
    const QVector<Preset> presets =
        Presets::fromJson(ConfigManager::instance()->value(Keys::apiPresets).toArray());
    const int matched = Presets::matchValues(presets, Presets::capture(Keys::apiPresetFields()),
                                            Keys::apiPresetFields());
    const QString current = matched >= 0 ? presets.at(matched).name : QString();
    const auto actions = m_apiPresetMenu->actions();
    for (QAction* action : actions) {
        QSignalBlocker blocker(action);
        action->setChecked(!action->data().toString().isEmpty() && action->data().toString() == current);
    }
}

void MainWindow::applyAlwaysOnTop(bool onTop)
{
    Qt::WindowFlags flags = windowFlags();
    flags.setFlag(Qt::WindowStaysOnTopHint, onTop);
    if (QWindow* handle = windowHandle()) {
        handle->setFlags(flags);
        // The window does not write the new flags back into the widget's cache.
        overrideWindowFlags(flags);
        return;
    }
    // No native window yet, so setWindowFlag() cannot hide or recreate one.
    setWindowFlag(Qt::WindowStaysOnTopHint, onTop);
}

void MainWindow::applyEditorFonts()
{
    const QFont editorFont = AppFonts::editorFont();
    m_sourceEdit->setFont(editorFont);
    m_resultEdit->setFont(editorFont);
}

void MainWindow::applyClipboardWatching()
{
    disconnect(&m_clipboard, &SessionClipboard::changed, this, nullptr);
    m_clipboardTimer->stop();
    if (!m_clipboardAction->isChecked() && !m_yoloAction->isChecked())
        return;
    connect(&m_clipboard, &SessionClipboard::changed, this,
            [this]() { m_clipboardTimer->start(); });
}

void MainWindow::applyYoloMode(bool enabled)
{
    if (enabled) {
        m_activity.markReady();
        m_clipboardWritten.clear();
    } else {
        m_yolo.stop();
    }
    applyClipboardWatching();
    updateTrayIcon();
}

bool MainWindow::clipboardTextIsNewWork(const QString& text) const
{
    const QStringView candidate = QStringView(text).trimmed();
    return !candidate.isEmpty() && candidate != m_clipboardWritten;
}

void MainWindow::onConfigChanged(const QString& key)
{
    if (key == Keys::uiLanguage) {
        // Qt delivers LanguageChange when the translator is swapped, which runs
        // retranslateUi(); only the catalog-independent lists are rebuilt here.
        populateLanguageCombos();
        populateToneCombo();
        return;
    }
    if (key == Keys::uiAutoTranslate) {
        QSignalBlocker blocker(m_autoTranslateAction);
        m_autoTranslateAction->setChecked(ConfigManager::instance()->boolValue(key));
    } else if (key == Keys::uiAlwaysOnTop) {
        QSignalBlocker blocker(m_onTopAction);
        m_onTopAction->setChecked(ConfigManager::instance()->boolValue(key));
        applyAlwaysOnTop(ConfigManager::instance()->boolValue(key));
    } else if (key == Keys::uiFontSize) {
        applyEditorFonts();
    } else if (key == Keys::uiNotifications) {
        QSignalBlocker blocker(m_notificationsAction);
        m_notificationsAction->setChecked(ConfigManager::instance()->boolValue(key));
    } else if (key == Keys::clipboardMonitor) {
        QSignalBlocker blocker(m_clipboardAction);
        m_clipboardAction->setChecked(ConfigManager::instance()->boolValue(key));
        applyClipboardWatching();
    } else if (key == Keys::historyMaxRecords) {
        m_history.setMaxRecords(ConfigManager::instance()->intValue(key));
    } else if (key == Keys::translationSourceLang || key == Keys::translationTargetLang) {
        QComboBox* combo = key == Keys::translationSourceLang ? m_sourceLang : m_targetLang;
        QSignalBlocker blocker(combo);
        const int index = combo->findData(ConfigManager::instance()->stringValue(key));
        if (index >= 0)
            combo->setCurrentIndex(index);
    } else if (key == Keys::translationTone || key == Keys::translationCustomTones) {
        populateToneCombo();
    } else if (key == Keys::apiPresets) {
        rebuildApiPresetMenu();
    } else if (Keys::apiPresetFields().contains(key)) {
        // Presets::apply() writes one changed() per field, so the selection is
        // re-derived on each; the last one settles it.
        syncApiPresetMenu();
    }
}

TranslationContext MainWindow::currentContext() const
{
    ConfigManager* config = ConfigManager::instance();
    TranslationContext context;
    context.sourceText = m_sourceEdit->toPlainText();
    context.sourceLang = m_sourceLang->currentData().toString();
    context.targetLang = m_targetLang->currentData().toString();
    context.tone = m_tone->currentData().toString();
    context.style = config->stringValue(Keys::translationStyle);
    context.background = config->stringValue(Keys::translationBackground);
    context.glossaryEnabled = config->boolValue(Keys::glossaryEnabled);
    context.glossary = Glossary::loadFromConfig().entries;
    return context;
}

void MainWindow::onSourceChanged()
{
    // Editing the source answers the pending clipboard read with other work.
    m_clipboardReadPending = false;
    updateCountLabel();
    if (m_autoTranslateAction->isChecked()) {
        m_debounce->start();
    }
}

void MainWindow::translateNow()
{
    m_debounce->stop();
    if (!carriesText(m_sourceEdit->toPlainText())) {
        setStatus(Status::EmptySource);
        return;
    }
    endResultStream();
    const TranslationContext context = currentContext();
    // The record describes the request, not the controls as they stand when the
    // answer lands.
    TranslationRecord record;
    record.timestamp = QDateTime::currentSecsSinceEpoch();
    record.sourceLang = context.sourceLang;
    record.targetLang = context.targetLang;
    record.source = context.sourceText;
    record.tone = context.tone;
    m_pendingRecord = record;
    setStatus(Status::Translating);
    m_engine.translateText(context);
}

void MainWindow::setBusy(bool busy)
{
    m_translateAction->setEnabled(!busy);
}

void MainWindow::setStatus(Status status, const QString& argument)
{
    m_statusPresenter->show([this, status, argument]() { return statusText(status, argument); });
}

void MainWindow::setStatusFailure(const ApiClient::Error& failure)
{
    m_statusPresenter->showFailure([this, failure]() { return failure.text(); });
}

QString MainWindow::statusText(Status status, const QString& argument) const
{
    switch (status) {
    case Status::Ready: return tr("Ready");
    case Status::Translating: return tr("Translating...");
    case Status::Finished: return tr("Translation finished");
    case Status::Cancelled: return tr("Translation cancelled");
    case Status::ReplacementSkipped: return tr("Translation has changed; replacement skipped");
    case Status::Restored: return tr("Restored previous translation");
    case Status::Reapplied: return tr("Re-applied translation");
    case Status::Copied: return tr("Translation copied to clipboard");
    case Status::EmptySource: return tr("Enter text to translate");
    case Status::NothingToExport: return tr("Nothing to export");
    case Status::Exported: return tr("Exported to %1").arg(argument);
    case Status::CannotWrite: return tr("Cannot write file: %1").arg(argument);
    }
    return QString();
}

bool MainWindow::statusIsError(Status status)
{
    return status == Status::NothingToExport || status == Status::CannotWrite;
}

void MainWindow::updateCountLabel()
{
    m_countLabel->setText(
        tr("%n character(s)", nullptr, m_sourceEdit->document()->characterCount() - 1));
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
        return;
    }
    // A font or style change moves the controls without resizing the window.
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
        QTimer::singleShot(0, this, [this]() {
            updateSwapButtonMetrics();
            updateSwapButtonGeometry();
        });
        return;
    }
    if (event->type() != QEvent::ActivationChange || !m_clipboardReadPending || !isActiveWindow())
        return;
    m_clipboardReadPending = false;
    takeClipboardText();
}

void MainWindow::swapLanguages()
{
    discardResultStream();
    const QString sourceCode = m_sourceLang->currentData().toString();
    const QString targetCode = m_targetLang->currentData().toString();
    if (sourceCode == QLatin1String("auto")) {
        const int resolvedTarget =
            m_targetLang->findData(Languages::resolveAuto(m_sourceEdit->toPlainText(), targetCode));
        if (resolvedTarget >= 0)
            m_targetLang->setCurrentIndex(resolvedTarget);
    } else {
        const int newTarget = m_targetLang->findData(sourceCode);
        if (newTarget >= 0)
            m_targetLang->setCurrentIndex(newTarget);
        const int newSource = m_sourceLang->findData(targetCode);
        if (newSource >= 0)
            m_sourceLang->setCurrentIndex(newSource);
    }
    const QString sourceText = m_sourceEdit->toPlainText();
    const QString resultText = m_resultEdit->result();
    if (!resultText.isEmpty()) {
        m_sourceEdit->setPlainText(resultText);
        setResultText(sourceText);
    }
}

void MainWindow::pasteSource()
{
    const QString text = m_clipboard.text();
    if (carriesText(text))
        m_sourceEdit->setPlainText(text);
    m_sourceEdit->setFocus();
}

void MainWindow::onClipboardTimeout()
{
    const QString text = m_clipboard.text();
    if (!clipboardTextIsNewWork(text))
        return;
    if (m_clipboardAction->isChecked())
        m_sourceEdit->setPlainText(text);
    if (m_yoloAction->isChecked()) {
        m_yolo.translate(text, currentContext());
        updateStopAction();
    }
}

void MainWindow::translateClipboard()
{
    bringToFront(this);
    takeClipboardText();
}

// An empty clipboard waits for the window to become active, which is when a copy made
// elsewhere reaches the application.
void MainWindow::takeClipboardText()
{
    const QString text = m_clipboard.text();
    if (!carriesText(text)) {
        m_clipboardReadPending = true;
        return;
    }
    m_sourceEdit->setPlainText(text);
    translateNow();
}

void MainWindow::writeClipboard(const QString& text)
{
    m_clipboardWritten = text.trimmed();
    m_clipboard.setText(text);
}

void MainWindow::updateStopAction()
{
    m_stopAction->setEnabled(m_engine.isBusy() || m_yolo.isRunning());
}

void MainWindow::stopTranslations()
{
    // A request the pane has not sent yet is dropped with the ones in flight.
    m_debounce->stop();
    m_clipboardTimer->stop();
    // A candidate lookup leaves the engine busy without a translation to cancel.
    if (m_pendingRecord) {
        m_engine.stop();
        setStatus(Status::Cancelled);
    }
    m_engine.cancelCandidates();
    m_yolo.stop();
    m_activity.markReady();
}

void MainWindow::stopAllTranslations()
{
    stopTranslations();
    if (m_documentWindow)
        m_documentWindow->stopTranslation();
}

void MainWindow::updateTrayIcon()
{
    if (!m_tray)
        return;
    m_tray->setIcon(m_yoloAction->isChecked()
                        ? AppIcons::tray(m_trayFamily, m_activity.state())
                        : AppIcons::trayIdle(m_trayFamily));
}

void MainWindow::toggleVisible()
{
    if (isVisible() && isActiveWindow()) {
        hide();
        return;
    }
    bringToFront(this);
}

void MainWindow::appendStreamedResult(const QString& piece)
{
    if (!m_streamOpen) {
        m_streamOrigin = m_resultEdit->result();
        m_streamOpen = true;
        m_resultEdit->setResult(QString());
    }
    m_resultEdit->appendResult(piece);
}

void MainWindow::endResultStream()
{
    if (m_streamOpen)
        setResultText(m_resultEdit->result());
}

void MainWindow::discardResultStream()
{
    // A candidate lookup is not a run this abandons.
    if (!m_pendingRecord)
        return;
    // Left uncommitted, because the caller's own edit is the undo entry.
    m_streamOpen = false;
    m_streamOrigin.clear();
    m_engine.stop();
    setStatus(Status::Ready);
}

void MainWindow::setResultText(const QString& text)
{
    // A streamed run is one undo entry, opened with the text the pane held before
    // it.
    if (m_streamOpen) {
        m_streamOpen = false;
        const QString origin = std::exchange(m_streamOrigin, QString());
        // The final text differs only by trimming, so it is applied in place instead
        // of replacing the document.
        if (m_resultEdit->result() != text)
            m_resultEdit->updateResult(text);
        m_resultHistory.push(new ResultTextCommand(m_resultEdit, origin, text, true));
        return;
    }
    const QString before = m_resultEdit->result();
    if (before == text)
        return;
    m_resultHistory.push(new ResultTextCommand(m_resultEdit, before, text, false));
}

void MainWindow::copyResult()
{
    const QString text = m_resultEdit->result();
    if (text.isEmpty())
        return;
    writeClipboard(text);
    setStatus(Status::Copied);
}

void MainWindow::exportTranslation()
{
    const QString text = m_resultEdit->result();
    if (text.isEmpty()) {
        setStatus(Status::NothingToExport);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Export translation"),
                                                      QStringLiteral("translation.txt"));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatus(Status::CannotWrite, path);
        return;
    }
    const QByteArray data = text.toUtf8();
    if (file.write(data) != data.size() || !file.commit()) {
        setStatus(Status::CannotWrite, path);
        return;
    }
    setStatus(Status::Exported, path);
}

void MainWindow::showSettingsDialog()
{
    SettingsDialog dialog(&m_history, this);
    dialog.exec();
}

void MainWindow::showDocumentWindow()
{
    if (!m_documentWindow) {
        m_documentWindow = std::make_unique<DocumentTranslationWindow>(
            [this]() { return currentContext(); });
    }
    bringToFront(m_documentWindow.get());
}

void MainWindow::showHistoryDialog()
{
    HistoryDialog dialog(&m_history, this);
    connect(&dialog, &HistoryDialog::reuseRequested, this, [this](const TranslationRecord& record) {
        discardResultStream();
        m_sourceEdit->setPlainText(record.source);
        const int sourceIndex = m_sourceLang->findData(record.sourceLang);
        if (sourceIndex >= 0)
            m_sourceLang->setCurrentIndex(sourceIndex);
        const int targetIndex = m_targetLang->findData(record.targetLang);
        if (targetIndex >= 0)
            m_targetLang->setCurrentIndex(targetIndex);
        setResultText(record.target);
    });
    dialog.exec();
}

void MainWindow::retranslateUi()
{
    setWindowTitle(tr("%1 Translator").arg(QCoreApplication::applicationName()));

    m_fileMenu->setTitle(tr("&File"));
    m_documentAction->setText(tr("Open document..."));
    m_documentAction->setShortcut(QKeySequence::Open);
    m_exportAction->setText(tr("Export translation..."));
    m_exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    m_exitAction->setText(tr("Exit %1").arg(QCoreApplication::applicationName()));
    m_exitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));

    m_editMenu->setTitle(tr("&Edit"));
    m_glossaryAction->setText(tr("Glossary..."));
    m_glossaryAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    m_toneAction->setText(tr("Manage tones..."));
    m_toneAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    m_historyAction->setText(tr("History..."));
    m_historyAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
    m_apiPresetAction->setText(tr("Manage API presets..."));
    m_apiPresetAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    m_contextPresetAction->setText(tr("Manage context presets..."));
    m_contextPresetAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));

    m_viewMenu->setTitle(tr("&View"));
    m_onTopAction->setText(tr("Always on top"));
    m_notificationsAction->setText(tr("Enable notifications"));
    m_languageMenu->setTitle(tr("Interface language"));
    for (QAction* action : m_languageMenu->actions()) {
        if (action->data().toString() == Keys::uiLanguageAuto)
            action->setText(tr("Follow system"));
    }
    syncLanguageMenu();

    m_toolsMenu->setTitle(tr("&Tools"));
    m_autoTranslateAction->setText(tr("Auto translate"));
    m_clipboardAction->setText(tr("Monitor clipboard"));
    m_yoloAction->setText(tr("YOLO mode"));
    m_apiPresetMenu->setTitle(tr("API preset"));
    m_settingsAction->setText(tr("Settings..."));

    m_helpMenu->setTitle(tr("&Help"));
    m_aboutAction->setText(tr("About %1").arg(QCoreApplication::applicationName()));

    m_translateAction->setText(tr("Start translation"));
    m_translateAction->setIconText(tr("Translate"));
    m_translateAction->setToolTip(tr("Start translation"));
    m_stopAction->setText(tr("Stop"));
    m_toolBar->setWindowTitle(tr("Toolbar"));
    m_documentAction->setIconText(tr("Document"));
    m_historyAction->setIconText(tr("History"));
    m_settingsAction->setIconText(tr("Settings"));
    m_yoloAction->setIconText(tr("YOLO"));
    m_glossaryAction->setIconText(tr("Glossary"));
    m_apiPresetAction->setIconText(tr("API preset"));
    m_contextPresetAction->setIconText(tr("Context preset"));
    m_stopAction->setToolTip(tr("Stop translation"));
    m_documentAction->setToolTip(tr("Document translation"));
    m_historyAction->setToolTip(tr("Translation history"));
    m_yoloAction->setToolTip(tr("Translate the clipboard as it changes and write the result back to it"));
    m_glossaryAction->setToolTip(tr("Manage glossary"));
    m_contextPresetAction->setToolTip(tr("Manage context presets"));
    m_settingsAction->setToolTip(tr("Open settings"));
    m_undoAction->setText(tr("Restore previous translation"));
    m_redoAction->setText(tr("Redo translation"));
    m_swapAction->setText(tr("Swap languages"));
    m_clearAction->setText(tr("Clear"));
    m_pasteAction->setText(tr("Paste"));
    m_copyAction->setText(tr("Copy"));
    m_clearResultAction->setText(tr("Clear"));

    m_sourceLang->setToolTip(tr("Source language"));
    m_tone->setToolTip(tr("Tone"));
    m_targetLang->setToolTip(tr("Target language"));

    m_sourceEdit->setPlaceholderText(tr("Enter text to translate"));

    if (m_apiPresetPlaceholder)
        m_apiPresetPlaceholder->setText(tr("No presets"));
    m_statusPresenter->refresh();
    updateCountLabel();

    if (m_tray) {
        m_trayShowHideAction->setText(tr("Show/Hide window"));
        m_trayTranslateClipAction->setText(tr("Translate clipboard"));
        m_trayDocumentAction->setText(tr("Document translation"));
        m_trayStopAction->setText(tr("Stop all"));
        m_trayExitAction->setText(tr("Exit %1").arg(QCoreApplication::applicationName()));
    }
}
