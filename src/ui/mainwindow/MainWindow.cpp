#include "MainWindow.h"

#include "ui/widgets/CandidatePopup.h"
#include "ui/dialogs/AboutDialog.h"
#include "ui/dialogs/DocumentDialog.h"
#include "ui/dialogs/GlossaryDialog.h"
#include "ui/dialogs/HistoryDialog.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/dialogs/ToneDialog.h"
#include "ui/widgets/AppIcons.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/models/Glossary.h"
#include "core/translation/Language.h"
#include "core/translation/Tone.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWindow>
#include <QVBoxLayout>

namespace {
constexpr int kMaxResultSnapshots = 30;
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_engine(this)
    , m_history(ConfigManager::instance()->historyFilePath(), this)
{
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    setUnifiedTitleAndToolBarOnMac(true);
    restoreGeometryFromConfig();

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
    statusBar()->addWidget(m_statusLabel, 1);
    setStatusMessage(tr("Ready"), false);

    m_popup = new CandidatePopup(&m_engine, this);
    connect(m_resultEdit, &TranslationEdit::wordRequested, this,
            [this](const QString& word, const QPoint& globalPos, const QTextCursor& cursor) {
                m_popup->openFor(word, globalPos,
                                 m_sourceEdit->toPlainText(), m_resultEdit->result(),
                                 m_targetLang->currentData().toString(), cursor);
            });
    connect(m_popup, &CandidatePopup::candidateChosen, this,
            [this](const QString& replaceTarget, const QString& replacement, const QTextCursor& cursor) {
                pushResultSnapshot();
                if (!m_resultEdit->replaceWordAt(cursor, replaceTarget, replacement)) {
                    m_resultSnapshots.removeLast();
                    updateUndoRedoActions();
                    setStatusMessage(tr("Translation has changed; replacement skipped"), false);
                }
            });

    connect(&m_engine, &TranslationEngine::partialResult, this, [this](const QString& text) {
        m_resultEdit->setResult(text);
        auto* bar = m_resultEdit->verticalScrollBar();
        bar->setValue(bar->maximum());
    });
    connect(&m_engine, &TranslationEngine::finished, this, [this](const QString& text) {
        m_resultEdit->setResult(text);
        setStatusMessage(tr("Translation finished"), false);
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
    connect(&m_engine, &TranslationEngine::error, this, [this](const QString& message) {
        setStatusMessage(message, true);
    });
    connect(&m_engine, &TranslationEngine::stateChanged, this, [this](bool busy) {
        setBusy(busy);
        if (busy)
            setStatusMessage(tr("Translating..."), false);
    });
    connect(&m_engine, &TranslationEngine::stopped, this, [this]() {
        setStatusMessage(tr("Translation cancelled"), false);
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
    saveGeometryToConfig();
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
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undoResult);
    m_redoAction = new QAction(pane);
    m_redoAction->setIcon(AppIcons::redo());
    m_redoAction->setEnabled(false);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::redoResult);
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
        pushResultSnapshot();
        m_resultEdit->setResult(QString());
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
    const QList<QPair<QString, QString>> languageOptions = {
        {tr("Follow system"), QStringLiteral("auto")},
        {QStringLiteral("English"), QStringLiteral("en")},
        {QStringLiteral("简体中文"), QStringLiteral("zh")}
    };
    for (const QPair<QString, QString>& option : languageOptions) {
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
        saveGeometryToConfig();
        ConfigManager::instance()->flush();
        qApp->quit();
    });
    connect(m_glossaryAction, &QAction::triggered, this, [this]() {
        GlossaryDialog dialog(this);
        dialog.exec();
    });
    connect(m_toneAction, &QAction::triggered, this, [this]() {
        ConfigManager* config = ConfigManager::instance();
        ToneDialog dialog(config->value(Keys::translationCustomTones).toArray(),
                          config->resolvedUiLanguage(), this);
        if (dialog.exec() == QDialog::Accepted)
            config->setValue(Keys::translationCustomTones, ToneDialog::toJson(dialog.customTones()));
    });
    connect(m_historyAction, &QAction::triggered, this, &MainWindow::showHistoryDialog);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::showSettingsDialog);
    connect(m_aboutAction, &QAction::triggered, this, [this]() {
        AboutDialog dialog(this);
        dialog.exec();
    });
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/icons/app.svg")), this);
    QMenu* menu = new QMenu();
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
    const QString uiLanguage = ConfigManager::instance()->resolvedUiLanguage();
    const QString source = m_sourceLang ? m_sourceLang->currentData().toString()
                                        : ConfigManager::instance()->stringValue(Keys::translationSourceLang);
    const QString target = m_targetLang ? m_targetLang->currentData().toString()
                                        : ConfigManager::instance()->stringValue(Keys::translationTargetLang);
    if (m_sourceLang) {
        QSignalBlocker blocker(m_sourceLang);
        m_sourceLang->clear();
        for (const LangItem& lang : Languages::all())
            m_sourceLang->addItem(Languages::displayName(lang.code, uiLanguage), lang.code);
        const int index = m_sourceLang->findData(source.isEmpty() ? ConfigManager::instance()->stringValue(Keys::translationSourceLang) : source);
        m_sourceLang->setCurrentIndex(index < 0 ? 0 : index);
    }
    if (m_targetLang) {
        QSignalBlocker blocker(m_targetLang);
        m_targetLang->clear();
        for (const LangItem& lang : Languages::all()) {
            if (lang.code == QLatin1String("auto"))
                continue;
            m_targetLang->addItem(Languages::displayName(lang.code, uiLanguage), lang.code);
        }
        const int index = m_targetLang->findData(target.isEmpty() ? ConfigManager::instance()->stringValue(Keys::translationTargetLang) : target);
        m_targetLang->setCurrentIndex(index < 0 ? 1 : index);
    }
}

void MainWindow::populateToneCombo()
{
    if (!m_tone)
        return;
    const QString uiLanguage = ConfigManager::instance()->resolvedUiLanguage();
    QSignalBlocker blocker(m_tone);
    m_tone->clear();
    for (const ToneItem& tone : Tones::presets())
        m_tone->addItem(Tones::presetDisplayName(tone.key, uiLanguage), tone.key);
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
    QFont editorFont = QApplication::font();
    editorFont.setPointSize(ConfigManager::instance()->intValue(Keys::uiFontSize) + 1);
    m_sourceEdit->setFont(editorFont);
    m_resultEdit->setFont(editorFont);
}

void MainWindow::applyClipboardMonitoring(bool enabled)
{
    disconnect(QApplication::clipboard(), &QClipboard::dataChanged, this, nullptr);
    m_clipboardTimer->stop();
    if (enabled) {
        connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this]() {
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
        populateLanguageCombos();
        populateToneCombo();
        retranslateUi();
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
    const QJsonArray prefs = config->value(Keys::translationPreferences).toArray();
    for (const QJsonValue& value : prefs)
        context.preferences << value.toString();
    context.uiLanguage = config->resolvedUiLanguage();
    return context;
}

void MainWindow::onSourceChanged()
{
    const int count = m_sourceEdit->toPlainText().size();
    m_countLabel->setText(tr("%n character(s)", nullptr, count));
    if (m_autoTranslateAction->isChecked()) {
        m_debounce->start();
    }
}

void MainWindow::translateNow()
{
    m_debounce->stop();
    const QString text = m_sourceEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        setStatusMessage(tr("Enter text to translate"), false);
        return;
    }
    pushResultSnapshot();
    m_engine.translateText(currentContext());
}

void MainWindow::setBusy(bool busy)
{
    m_translateAction->setEnabled(!busy);
    m_stopAction->setEnabled(busy);
}

void MainWindow::setStatusMessage(const QString& message, bool isError)
{
    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet(isError ? QStringLiteral("color: red;") : QString());
}

void MainWindow::swapLanguages()
{
    pushResultSnapshot();
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
        m_resultEdit->setResult(sourceText);
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
    if (const QScreen* screen = QGuiApplication::screenAt(QCursor::pos()))
        move(screen->geometry().center() - rect().center());
    const QString clipboardText = QApplication::clipboard()->text().trimmed();
    if (!clipboardText.isEmpty() && clipboardText != m_lastClipboard
        && clipboardText != m_sourceEdit->toPlainText().trimmed()) {
        m_sourceEdit->setPlainText(clipboardText);
        translateNow();
    }
}

void MainWindow::pushResultSnapshot()
{
    const QString current = m_resultEdit->toPlainText();
    if (current.isEmpty())
        return;
    if (!m_resultSnapshots.isEmpty() && m_resultSnapshots.last() == current)
        return;
    m_resultSnapshots.append(current);
    while (m_resultSnapshots.size() > kMaxResultSnapshots)
        m_resultSnapshots.removeFirst();
    m_redoSnapshots.clear();
    updateUndoRedoActions();
}

void MainWindow::undoResult()
{
    if (m_resultSnapshots.isEmpty())
        return;
    const QString current = m_resultEdit->toPlainText();
    if (!current.isEmpty()
        && (m_redoSnapshots.isEmpty() || m_redoSnapshots.last() != current))
        m_redoSnapshots.append(current);
    while (m_redoSnapshots.size() > kMaxResultSnapshots)
        m_redoSnapshots.removeFirst();
    m_resultEdit->setResult(m_resultSnapshots.takeLast());
    updateUndoRedoActions();
    setStatusMessage(tr("Restored previous translation"), false);
}

void MainWindow::redoResult()
{
    if (m_redoSnapshots.isEmpty())
        return;
    const QString current = m_resultEdit->toPlainText();
    if (!current.isEmpty()
        && (m_resultSnapshots.isEmpty() || m_resultSnapshots.last() != current))
        m_resultSnapshots.append(current);
    while (m_resultSnapshots.size() > kMaxResultSnapshots)
        m_resultSnapshots.removeFirst();
    m_resultEdit->setResult(m_redoSnapshots.takeLast());
    updateUndoRedoActions();
    setStatusMessage(tr("Re-applied translation"), false);
}

void MainWindow::updateUndoRedoActions()
{
    m_undoAction->setEnabled(!m_resultSnapshots.isEmpty());
    m_redoAction->setEnabled(!m_redoSnapshots.isEmpty());
}

void MainWindow::copyResult()
{
    const QString text = m_resultEdit->result();
    if (text.isEmpty())
        return;
    m_lastClipboard = text.trimmed();
    QApplication::clipboard()->setText(text);
    setStatusMessage(tr("Translation copied to clipboard"), false);
}

void MainWindow::exportTranslation()
{
    const QString text = m_resultEdit->result();
    if (text.isEmpty()) {
        setStatusMessage(tr("Nothing to export"), true);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Export translation"),
                                                      QStringLiteral("translation.txt"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatusMessage(tr("Cannot write file: %1").arg(path), true);
        return;
    }
    file.write(text.toUtf8());
    setStatusMessage(tr("Exported to %1").arg(path), false);
}

void MainWindow::showSettingsDialog()
{
    SettingsDialog dialog(this);
    dialog.exec();
}

void MainWindow::showHistoryDialog()
{
    HistoryDialog dialog(&m_history, this);
    connect(&dialog, &HistoryDialog::reuseRequested, this, [this](const TranslationRecord& record) {
        pushResultSnapshot();
        m_sourceEdit->setPlainText(record.source);
        const int sourceIndex = m_sourceLang->findData(record.sourceLang);
        if (sourceIndex >= 0)
            m_sourceLang->setCurrentIndex(sourceIndex);
        const int targetIndex = m_targetLang->findData(record.targetLang);
        if (targetIndex >= 0)
            m_targetLang->setCurrentIndex(targetIndex);
        m_resultEdit->setResult(record.target);
    });
    dialog.exec();
}

void MainWindow::restoreGeometryFromConfig()
{
    const QString encoded = ConfigManager::instance()->stringValue(Keys::uiWindowGeometry);
    if (encoded.isEmpty()) {
        resize(1080, 680);
        return;
    }
    const QByteArray data = QByteArray::fromBase64(encoded.toLatin1());
    if (!restoreGeometry(data))
        resize(1080, 680);
}

void MainWindow::saveGeometryToConfig()
{
    ConfigManager::instance()->setValue(Keys::uiWindowGeometry,
                                        QString::fromLatin1(saveGeometry().toBase64()));
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
    m_historyAction->setText(tr("History..."));
    m_historyAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));

    m_viewMenu->setTitle(tr("&View"));
    m_autoTranslateAction->setText(tr("Auto translate"));
    m_onTopAction->setText(tr("Always on top"));
    m_languageMenu->setTitle(tr("Interface language"));
    for (QAction* action : m_languageMenu->actions()) {
        if (action->data().toString() == QLatin1String("auto"))
            action->setText(tr("Follow system"));
    }
    syncLanguageMenu();

    m_toolsMenu->setTitle(tr("&Tools"));
    m_clipboardAction->setText(tr("Monitor clipboard"));
    m_settingsAction->setText(tr("Settings..."));

    m_helpMenu->setTitle(tr("&Help"));
    m_aboutAction->setText(tr("About RiipL"));

    m_translateAction->setText(tr("Translate"));
    m_stopAction->setText(tr("Stop"));
    m_documentAction->setText(tr("Document"));
    m_historyAction->setText(tr("History"));
    m_settingsAction->setText(tr("Settings"));
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
    if (m_trayMenu) {
        m_trayShowHideAction->setText(tr("Show/Hide window"));
        m_trayTranslateClipAction->setText(tr("Translate clipboard"));
    }
}
