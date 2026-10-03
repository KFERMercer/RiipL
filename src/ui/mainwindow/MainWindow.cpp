#include "MainWindow.h"

#include "ui/widgets/CandidatePopup.h"
#include "ui/dialogs/DocumentDialog.h"
#include "ui/dialogs/ApiPresetDialog.h"
#include "ui/dialogs/GlossaryDialog.h"
#include "ui/dialogs/HistoryDialog.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/dialogs/ToneDialog.h"
#include "ui/widgets/AppFonts.h"
#include "ui/widgets/AppIcons.h"
#include "ui/widgets/ThemeColors.h"
#include "ui/widgets/ConfigEditors.h"
#include "ui/widgets/WindowState.h"
#include "core/config/ApiPreset.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/models/Glossary.h"
#include "core/translation/Language.h"
#include "core/translation/Tone.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QByteArray>
#include <QClipboard>
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
#include <QPalette>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScreen>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QWindow>
#include <QVBoxLayout>

#include <utility>

namespace {

constexpr int kMaxResultSteps = 30;

constexpr char kProjectUrl[] = "https://github.com/KFERMercer/RiipL";

// One undoable edit of the translation pane.
class ResultTextCommand : public QUndoCommand
{
public:
    ResultTextCommand(TranslationEdit* edit, const QString& before, const QString& after)
        : m_edit(edit)
        , m_before(before)
        , m_after(after)
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
    bool m_applied = true;
};

}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_engine(this)
    , m_history(ConfigManager::instance()->historyFilePath(), this)
{
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
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
    m_splitter->setSizes({600, 600});
    centralLayout->addWidget(m_splitter);
    setCentralWidget(centralArea);

    if (!WindowState::track(this, WindowState::Id::main, m_splitter)) {
        const QRect available = screen()->availableGeometry();
        resize(available.width() * 2 / 5, available.height() / 2);
    }

    m_swapAction = new QAction(this);
    m_swapAction->setIcon(AppIcons::swapHorizontal());
    connect(m_swapAction, &QAction::triggered, this, &MainWindow::swapLanguages);

    m_swapButton = new QToolButton(centralArea);
    m_swapButton->setDefaultAction(m_swapAction);
    m_swapButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    connect(m_splitter, &QSplitter::splitterMoved, this,
            &MainWindow::updateSwapButtonGeometry);
    QTimer::singleShot(0, this, &MainWindow::updateSwapButtonGeometry);

    buildMenus();
    buildTray();

    m_statusLabel = new QLabel(this);
    // A provider words its own failures, so this label takes no markup.
    m_statusLabel->setTextFormat(Qt::PlainText);
    statusBar()->addWidget(m_statusLabel, 1);
    setStatus(Status::Ready);

    m_popup = new CandidatePopup(&m_engine, this);
    connect(m_resultEdit, &TranslationEdit::wordRequested, this,
            [this](const QString& word, int selectionStart, int selectionEnd,
                   const QPoint& globalPos) {
                TranslationContext context = currentContext();
                m_candidateOrigin = m_resultEdit->result();
                context.translatedText = m_candidateOrigin;
                m_popup->openFor(word, selectionStart, selectionEnd, globalPos, context);
            });
    connect(m_popup, &CandidatePopup::candidateChosen, this,
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
                    new ResultTextCommand(m_resultEdit, before, m_resultEdit->result()));
            });

    connect(&m_engine, &TranslationEngine::partialDelta, this, &MainWindow::appendStreamedResult);
    connect(&m_engine, &TranslationEngine::finished, this, [this](const QString& text) {
        setResultText(text);
        setStatus(Status::Finished);
        if (ConfigManager::instance()->boolValue(Keys::historyEnabled)) {
            TranslationRecord record;
            record.timestamp = QDateTime::currentSecsSinceEpoch();
            record.sourceLang = m_sourceLang->currentData().toString();
            record.targetLang = m_targetLang->currentData().toString();
            record.source = m_sourceEdit->toPlainText();
            record.target = text;
            record.tone = m_tone->currentData().toString();
            m_history.addRecord(record);
        }
    });
    connect(&m_engine, &TranslationEngine::errorOccurred, this, [this](const ApiClient::Error& failure) {
        endResultStream();
        setStatusFailure(failure);
    });
    connect(&m_engine, &TranslationEngine::stateChanged, this, [this](bool busy) {
        setBusy(busy);
        if (busy)
            setStatus(Status::Translating);
    });
    connect(&m_engine, &TranslationEngine::stopped, this, [this]() {
        endResultStream();
        setStatus(Status::Cancelled);
    });

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(ConfigManager::instance()->intValue(Keys::uiAutoTranslateDelay));
    connect(m_debounce, &QTimer::timeout, this, &MainWindow::translateNow);

    m_clipboardTimer = new QTimer(this);
    m_clipboardTimer->setSingleShot(true);
    m_clipboardTimer->setInterval(ConfigManager::instance()->intValue(Keys::clipboardDelayMs));
    connect(m_clipboardTimer, &QTimer::timeout, this, &MainWindow::translateClipboard);

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

    applyClipboardMonitoring(ConfigManager::instance()->boolValue(Keys::clipboardMonitor));
    m_history.setMaxRecords(ConfigManager::instance()->intValue(Keys::historyMaxRecords));

    applyAlwaysOnTop(ConfigManager::instance()->boolValue(Keys::uiAlwaysOnTop));

    applyEditorFonts();

    retranslateUi();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_tray && ConfigManager::instance()->boolValue(Keys::uiMinimizeToTray)) {
        hide();
        event->ignore();
        return;
    }
    ConfigManager::instance()->flush();
    event->accept();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateSwapButtonGeometry();
}

QWidget* MainWindow::createLeftPane()
{
    auto* pane = new QWidget(this);
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* topRow = new QHBoxLayout();
    m_sourceLang = new QComboBox(pane);
    m_sourceLang->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    topRow->addWidget(m_sourceLang);
    topRow->addStretch(1);
    layout->addLayout(topRow);

    m_sourceEdit = new QPlainTextEdit(pane);
    m_sourceEdit->setPlaceholderText(tr("Enter text to translate"));
    layout->addWidget(m_sourceEdit, 1);

    auto* bottomRow = new QHBoxLayout();
    m_clearAction = new QAction(pane);
    m_pasteAction = new QAction(pane);
    auto* pasteButton = new QToolButton(pane);
    pasteButton->setDefaultAction(m_pasteAction);
    pasteButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* clearButton = new QToolButton(pane);
    clearButton->setDefaultAction(m_clearAction);
    clearButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bottomRow->addWidget(pasteButton);
    bottomRow->addWidget(clearButton);
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
    layout->setContentsMargins(6, 6, 6, 6);

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
        m_resultHistory.undo();
        setStatus(Status::Restored);
    });
    m_redoAction = new QAction(pane);
    m_redoAction->setIcon(AppIcons::redo());
    m_redoAction->setEnabled(false);
    connect(m_redoAction, &QAction::triggered, this, [this]() {
        m_resultHistory.redo();
        setStatus(Status::Reapplied);
    });
    // The window retranslates its own labels, so the stack only drives the enabled state.
    connect(&m_resultHistory, &QUndoStack::canUndoChanged, m_undoAction, &QAction::setEnabled);
    connect(&m_resultHistory, &QUndoStack::canRedoChanged, m_redoAction, &QAction::setEnabled);
    m_copyAction = new QAction(pane);
    m_clearResultAction = new QAction(pane);
    auto* undoButton = new QToolButton(pane);
    undoButton->setDefaultAction(m_undoAction);
    undoButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    auto* redoButton = new QToolButton(pane);
    redoButton->setDefaultAction(m_redoAction);
    redoButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    auto* clearResultButton = new QToolButton(pane);
    clearResultButton->setDefaultAction(m_clearResultAction);
    clearResultButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* copyButton = new QToolButton(pane);
    copyButton->setDefaultAction(m_copyAction);
    copyButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bottomRow->addWidget(undoButton);
    bottomRow->addWidget(redoButton);
    bottomRow->addStretch(1);
    bottomRow->addWidget(clearResultButton);
    bottomRow->addWidget(copyButton);
    layout->addLayout(bottomRow);

    connect(m_copyAction, &QAction::triggered, this, &MainWindow::copyResult);
    connect(m_clearResultAction, &QAction::triggered, this, [this]() {
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
    m_exitAction = m_fileMenu->addAction(QString());

    m_editMenu = menuBar()->addMenu(QString());
    m_glossaryAction = m_editMenu->addAction(QString());
    m_toneAction = m_editMenu->addAction(QString());
    m_historyAction = m_editMenu->addAction(QString());
    m_apiPresetAction = m_editMenu->addAction(QString());

    m_viewMenu = menuBar()->addMenu(QString());
    m_autoTranslateAction = m_viewMenu->addAction(QString());
    m_autoTranslateAction->setCheckable(true);
    m_autoTranslateAction->setChecked(ConfigManager::instance()->boolValue(Keys::uiAutoTranslate));
    m_onTopAction = m_viewMenu->addAction(QString());
    m_onTopAction->setCheckable(true);
    m_onTopAction->setChecked(ConfigManager::instance()->boolValue(Keys::uiAlwaysOnTop));
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
    m_clipboardAction = m_toolsMenu->addAction(QString());
    m_clipboardAction->setCheckable(true);
    m_clipboardAction->setChecked(ConfigManager::instance()->boolValue(Keys::clipboardMonitor));
    m_toolsMenu->addSeparator();
    m_apiPresetMenu = m_toolsMenu->addMenu(QString());
    m_apiPresetGroup = new QActionGroup(m_apiPresetMenu);
    m_apiPresetGroup->setExclusive(true);
    rebuildApiPresetMenu();
    m_toolsMenu->addSeparator();
    m_settingsAction = m_toolsMenu->addAction(QString());

    m_helpMenu = menuBar()->addMenu(QString());
    m_aboutAction = m_helpMenu->addAction(QString());

    QToolBar* toolBar = addToolBar(QString());
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_translateAction = new QAction(this);
    m_translateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Return")));
    m_translateButton = new QToolButton(toolBar);
    m_translateButton->setDefaultAction(m_translateAction);
    m_translateButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_translateButton->setStyleSheet(QStringLiteral(
        "QToolButton { color: white;"
        "              background-color: #007653;"
        "              border-radius: 0.25em;"
        "              padding: 0.25em; }"
        "QToolButton:hover { background-color: #009065; }"
        "QToolButton:pressed { background-color: #005C41; }"
        "QToolButton:disabled { color: rgba(255, 255, 255, 140);"
        "                       background-color: rgba(0, 118, 83, 110); }"));
    toolBar->addWidget(m_translateButton);
    connect(m_translateAction, &QAction::triggered, this, &MainWindow::translateNow);

    m_stopAction = toolBar->addAction(QString());
    m_stopAction->setEnabled(false);
    connect(m_stopAction, &QAction::triggered, this, [this]() { m_engine.stop(); });

    toolBar->addSeparator();
    toolBar->addAction(m_autoTranslateAction);
    toolBar->addAction(m_documentAction);
    toolBar->addAction(m_historyAction);
    toolBar->addSeparator();
    toolBar->addAction(m_settingsAction);

    connect(m_autoTranslateAction, &QAction::toggled, this, [this](bool checked) {
        ConfigManager::instance()->setValue(Keys::uiAutoTranslate, checked);
    });
    connect(m_onTopAction, &QAction::toggled, this, [this](bool checked) {
        ConfigManager::instance()->setValue(Keys::uiAlwaysOnTop, checked);
    });
    connect(m_clipboardAction, &QAction::toggled, this, [this](bool checked) {
        ConfigManager::instance()->setValue(Keys::clipboardMonitor, checked);
    });
    connect(m_documentAction, &QAction::triggered, this, [this]() {
        DocumentDialog dialog(currentContext(), this);
        dialog.exec();
    });
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::exportTranslation);
    connect(m_exitAction, &QAction::triggered, this, [this]() {
        ConfigManager::instance()->flush();
        qApp->quit();
    });
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
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::showSettingsDialog);
    connect(m_aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("About RiipL"),
                           tr("<b>RiipL %1</b><br/>"
                              "An AI-powered desktop translator.<br/>"
                              "Built with Qt %2.<br/>"
                              "Project homepage: <a href=\"%3\">%3</a>")
                               .arg(QCoreApplication::applicationVersion(),
                                    QString::fromLatin1(qVersion()),
                                    QString::fromLatin1(kProjectUrl)));
    });
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/icons/app.svg")), this);
    // The tray icon does not take ownership of its context menu, so the window
    // owns it.
    QMenu* menu = new QMenu(this);
    m_trayShowHideAction = menu->addAction(QString());
    menu->addAction(m_clipboardAction);
    m_trayTranslateClipAction = menu->addAction(QString());
    menu->addSeparator();
    menu->addAction(m_exitAction);
    m_tray->setContextMenu(menu);
    m_tray->setToolTip(QStringLiteral("RiipL"));
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger)
            toggleVisible();
    });
    connect(m_trayShowHideAction, &QAction::triggered, this, &MainWindow::toggleVisible);
    connect(m_trayTranslateClipAction, &QAction::triggered, this, [this]() {
        show();
        raise();
        activateWindow();
        translateClipboard();
    });
    m_tray->show();
    m_trayMenu = menu;
}

void MainWindow::updateSwapButtonGeometry()
{
    if (!m_splitter || !m_swapButton || !m_sourceLang)
        return;
    const QWidget* handle = m_splitter->handle(1);
    QWidget* base = m_swapButton->parentWidget();
    if (!handle || !base)
        return;
    const QPoint comboCenter = m_sourceLang->mapTo(base, m_sourceLang->rect().center());
    const QPoint handleCenter = m_splitter->mapTo(base, handle->geometry().center());
    m_swapButton->move(handleCenter.x() - m_swapButton->width() / 2,
                       comboCenter.y() - m_swapButton->height() / 2);
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
    const QVector<ApiPreset> presets =
        ApiPresets::fromJson(ConfigManager::instance()->value(Keys::apiPresets).toArray());
    for (const ApiPreset& preset : presets) {
        QAction* action = m_apiPresetMenu->addAction(preset.name);
        action->setData(preset.name);
        action->setCheckable(true);
        m_apiPresetGroup->addAction(action);
        connect(action, &QAction::triggered, this, [preset]() { ApiPresets::apply(preset); });
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

// Mirrors syncLanguageMenu: the entry whose stored values match the applied
// configuration is checked, and none is when the fields were edited by hand.
void MainWindow::syncApiPresetMenu()
{
    if (!m_apiPresetMenu)
        return;
    const QVector<ApiPreset> presets =
        ApiPresets::fromJson(ConfigManager::instance()->value(Keys::apiPresets).toArray());
    const int matched = ApiPresets::matchValues(presets, ApiPresets::capture());
    const QString current = matched >= 0 ? presets.at(matched).name : QString();
    const auto actions = m_apiPresetMenu->actions();
    for (QAction* action : actions) {
        QSignalBlocker blocker(action);
        action->setChecked(!action->data().toString().isEmpty() && action->data().toString() == current);
    }
}

void MainWindow::applyAlwaysOnTop(bool onTop)
{
    if (QWindow* handle = windowHandle()) {
        Qt::WindowFlags flags = handle->flags();
        flags.setFlag(Qt::WindowStaysOnTopHint, onTop);
        if (flags != handle->flags())
            handle->setFlags(flags);
        return;
    }
    setWindowFlag(Qt::WindowStaysOnTopHint, onTop);
}

void MainWindow::applyEditorFonts()
{
    const QFont editorFont = AppFonts::editorFont();
    m_sourceEdit->setFont(editorFont);
    m_resultEdit->setFont(editorFont);
}

void MainWindow::applyClipboardMonitoring(bool enabled)
{
    disconnect(QApplication::clipboard(), &QClipboard::dataChanged, this, nullptr);
    m_clipboardTimer->stop();
    if (enabled) {
        connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this]() {
            if (QApplication::clipboard()->ownsClipboard())
                return;
            if (QApplication::clipboard()->text().trimmed() == m_lastClipboard)
                return;
            m_clipboardTimer->start();
        });
        m_lastClipboard = QApplication::clipboard()->text().trimmed();
    }
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
    } else if (key == Keys::uiAutoTranslateDelay) {
        m_debounce->setInterval(ConfigManager::instance()->intValue(key));
    } else if (key == Keys::uiAlwaysOnTop) {
        QSignalBlocker blocker(m_onTopAction);
        m_onTopAction->setChecked(ConfigManager::instance()->boolValue(key));
        applyAlwaysOnTop(ConfigManager::instance()->boolValue(key));
    } else if (key == Keys::uiFontSize) {
        applyEditorFonts();
    } else if (key == Keys::clipboardMonitor) {
        QSignalBlocker blocker(m_clipboardAction);
        m_clipboardAction->setChecked(ConfigManager::instance()->boolValue(key));
        applyClipboardMonitoring(ConfigManager::instance()->boolValue(key));
    } else if (key == Keys::clipboardDelayMs) {
        m_clipboardTimer->setInterval(ConfigManager::instance()->intValue(key));
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
        // Applying a preset writes one changed() per field; the selection is
        // re-derived once they have all landed.
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
    updateCountLabel();
    if (m_autoTranslateAction->isChecked()) {
        m_debounce->start();
    }
}

void MainWindow::translateNow()
{
    m_debounce->stop();
    const QString text = m_sourceEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        setStatus(Status::EmptySource);
        return;
    }
    endResultStream();
    m_engine.translateText(currentContext());
}

void MainWindow::setBusy(bool busy)
{
    m_translateAction->setEnabled(!busy);
    m_stopAction->setEnabled(busy);
}

void MainWindow::setStatus(Status status, const QString& argument)
{
    m_status = status;
    m_statusArgument = argument;
    m_statusLabel->setText(statusText());
    if (statusIsError())
        ThemeColors::setTextColor(m_statusLabel, ThemeColors::errorText(m_statusLabel));
    else
        m_statusLabel->setPalette(QPalette());
}

void MainWindow::setStatusFailure(const ApiClient::Error& failure)
{
    m_statusFailure = failure;
    setStatus(Status::Failure);
}

QString MainWindow::statusText() const
{
    switch (m_status) {
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
    case Status::Exported: return tr("Exported to %1").arg(m_statusArgument);
    case Status::CannotWrite: return tr("Cannot write file: %1").arg(m_statusArgument);
    case Status::Failure: return m_statusFailure.text();
    }
    return QString();
}

bool MainWindow::statusIsError() const
{
    return m_status == Status::NothingToExport
        || m_status == Status::CannotWrite
        || m_status == Status::Failure;
}

void MainWindow::updateCountLabel()
{
    m_countLabel->setText(tr("%n character(s)", nullptr, m_sourceEdit->toPlainText().size()));
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
}

void MainWindow::swapLanguages()
{
    endResultStream();
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
    const QClipboard* clipboard = QApplication::clipboard();
    const QString text = clipboard->text();
    if (!text.isEmpty())
        m_sourceEdit->setPlainText(text);
    m_sourceEdit->setFocus();
}

void MainWindow::translateClipboard()
{
    const QString text = QApplication::clipboard()->text().trimmed();
    if (text.isEmpty() || text == m_lastClipboard)
        return;
    m_lastClipboard = text;
    m_sourceEdit->setPlainText(text);
    translateNow();
}

void MainWindow::toggleVisible()
{
    if (isVisible() && isActiveWindow()) {
        hide();
        return;
    }
    show();
    raise();
    activateWindow();
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

void MainWindow::setResultText(const QString& text)
{
    // A streamed run is one undo entry, opened with the text the pane held before
    // it.
    if (m_streamOpen) {
        m_streamOpen = false;
        const QString origin = std::exchange(m_streamOrigin, QString());
        if (m_resultEdit->result() != text)
            m_resultEdit->setResult(text);
        m_resultHistory.push(new ResultTextCommand(m_resultEdit, origin, text));
        return;
    }
    const QString before = m_resultEdit->result();
    if (before == text)
        return;
    m_resultEdit->setResult(text);
    m_resultHistory.push(new ResultTextCommand(m_resultEdit, before, text));
}

void MainWindow::copyResult()
{
    const QString text = m_resultEdit->result();
    if (text.isEmpty())
        return;
    m_lastClipboard = text.trimmed();
    QApplication::clipboard()->setText(text);
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

void MainWindow::showHistoryDialog()
{
    HistoryDialog dialog(&m_history, this);
    connect(&dialog, &HistoryDialog::reuseRequested, this, [this](const TranslationRecord& record) {
        endResultStream();
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
    setWindowTitle(tr("RiipL Translator"));

    m_fileMenu->setTitle(tr("&File"));
    m_documentAction->setText(tr("Open document..."));
    m_documentAction->setShortcut(QKeySequence::Open);
    m_exportAction->setText(tr("Export translation..."));
    m_exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    m_exitAction->setText(tr("Exit"));
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

    m_viewMenu->setTitle(tr("&View"));
    m_autoTranslateAction->setText(tr("Auto translate"));
    m_onTopAction->setText(tr("Always on top"));
    m_languageMenu->setTitle(tr("Interface language"));
    for (QAction* action : m_languageMenu->actions()) {
        if (action->data().toString() == Keys::uiLanguageAuto)
            action->setText(tr("Follow system"));
    }
    syncLanguageMenu();

    m_toolsMenu->setTitle(tr("&Tools"));
    m_clipboardAction->setText(tr("Monitor clipboard"));
    m_apiPresetMenu->setTitle(tr("API preset"));
    m_settingsAction->setText(tr("Settings..."));

    m_helpMenu->setTitle(tr("&Help"));
    m_aboutAction->setText(tr("About RiipL"));

    m_translateAction->setText(tr("Translate"));
    m_stopAction->setText(tr("Stop"));
    // A toolbar button built from an action shows its icon text, so these carry
    // the short labels the toolbar displays instead of the menu wording.
    m_documentAction->setIconText(tr("Document"));
    m_historyAction->setIconText(tr("History"));
    m_settingsAction->setIconText(tr("Settings"));
    m_translateAction->setToolTip(tr("Translate now (Ctrl+Return)"));
    m_stopAction->setToolTip(tr("Stop translation"));
    m_undoAction->setToolTip(tr("Restore previous translation"));
    m_redoAction->setToolTip(tr("Redo translation"));
    m_swapAction->setToolTip(tr("Swap languages"));
    m_clearAction->setText(tr("Clear"));
    m_pasteAction->setText(tr("Paste"));
    m_copyAction->setText(tr("Copy"));
    m_clearResultAction->setText(tr("Clear"));

    m_sourceEdit->setPlaceholderText(tr("Enter text to translate"));

    if (m_apiPresetPlaceholder)
        m_apiPresetPlaceholder->setText(tr("No presets"));
    m_statusLabel->setText(statusText());
    updateCountLabel();

    if (m_trayMenu) {
        m_trayShowHideAction->setText(tr("Show/Hide window"));
        m_trayTranslateClipAction->setText(tr("Translate clipboard"));
    }
}
