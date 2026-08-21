#include "MainWindow.h"

#include "ui/widgets/CandidatePopup.h"
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
#include <QDesktopServices>
#include <QDesktopServices>
#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr char kProjectUrl[] = "https://github.com/KFERMercer/RiipL";

}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_engine(this)
    , m_history(ConfigManager::instance()->historyFilePath(), this)
{
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    setUnifiedTitleAndToolBarOnMac(true);
    restoreGeometryFromConfig();

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(createLeftPane());
    splitter->addWidget(createRightPane());
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({600, 600});
    setCentralWidget(splitter);

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
                    updateUndoAction();
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
        setBusy(false);
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
        setBusy(false);
        setStatusMessage(message, true);
    });
    connect(&m_engine, &TranslationEngine::stateChanged, this, [this](bool busy) {
        if (busy)
            setStatusMessage(tr("Translating..."), false);
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
        if (!m_guard)
            ConfigManager::instance()->setValue(Keys::translationSourceLang, m_sourceLang->itemData(index).toString());
    });
    connect(m_targetLang, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_guard)
            ConfigManager::instance()->setValue(Keys::translationTargetLang, m_targetLang->itemData(index).toString());
    });
    connect(m_tone, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_guard)
            ConfigManager::instance()->setValue(Keys::translationTone, m_tone->itemData(index).toString());
    });

    m_hotkey.setSequence(ConfigManager::instance()->stringValue(Keys::hotkeySequence));
    connect(&m_hotkey, &GlobalHotkey::activated, this, &MainWindow::toggleVisible);
    applyHotkeyFromConfig();
    applyClipboardMonitoring(ConfigManager::instance()->boolValue(Keys::clipboardMonitor));
    m_history.setMaxRecords(ConfigManager::instance()->intValue(Keys::historyMaxRecords));

    if (ConfigManager::instance()->boolValue(Keys::uiAlwaysOnTop)) {
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
    }

    retranslateUi();
}

QWidget* MainWindow::createLeftPane()
{
    auto* pane = new QWidget(this);
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* topRow = new QHBoxLayout();
    m_sourceLang = new QComboBox(pane);
    m_sourceLang->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_swapAction = new QAction(pane);
    m_swapAction->setIcon(AppIcons::swapHorizontal());
    auto* swapButton = new QToolButton(pane);
    swapButton->setDefaultAction(m_swapAction);
    swapButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    topRow->addWidget(m_sourceLang);
    topRow->addWidget(swapButton);
    topRow->addStretch(1);
    layout->addLayout(topRow);

    m_sourceEdit = new QPlainTextEdit(pane);
    m_sourceEdit->setPlaceholderText(tr("Enter text to translate"));
    layout->addWidget(m_sourceEdit, 1);

    auto* bottomRow = new QHBoxLayout();
    m_clearAction = new QAction(pane);
    m_pasteAction = new QAction(pane);
    auto* clearButton = new QToolButton(pane);
    clearButton->setDefaultAction(m_clearAction);
    clearButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* pasteButton = new QToolButton(pane);
    pasteButton->setDefaultAction(m_pasteAction);
    pasteButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bottomRow->addWidget(clearButton);
    bottomRow->addWidget(pasteButton);
    bottomRow->addStretch(1);
    m_countLabel = new QLabel(QStringLiteral("0"), pane);
    bottomRow->addWidget(m_countLabel);
    layout->addLayout(bottomRow);

    connect(m_sourceEdit, &QPlainTextEdit::textChanged, this, &MainWindow::onSourceChanged);
    connect(m_swapAction, &QAction::triggered, this, &MainWindow::swapLanguages);
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
    topRow->addWidget(m_targetLang);
    topRow->addWidget(m_tone);
    topRow->addStretch(1);
    layout->addLayout(topRow);

    m_resultEdit = new TranslationEdit(pane);
    layout->addWidget(m_resultEdit, 1);

    auto* bottomRow = new QHBoxLayout();
    m_copyAction = new QAction(pane);
    m_undoAction = new QAction(pane);
    m_undoAction->setIcon(AppIcons::undo());
    m_undoAction->setEnabled(false);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undoResult);
    auto* undoButton = new QToolButton(pane);
    undoButton->setDefaultAction(m_undoAction);
    undoButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    bottomRow->addWidget(undoButton);
    auto* copyButton = new QToolButton(pane);
    copyButton->setDefaultAction(m_copyAction);
    copyButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bottomRow->addWidget(copyButton);
    bottomRow->addStretch(1);
    layout->addLayout(bottomRow);

    connect(m_copyAction, &QAction::triggered, this, &MainWindow::copyResult);
    return pane;
}

void MainWindow::buildMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(QString());
    m_documentAction = fileMenu->addAction(QString());
    m_exportAction = fileMenu->addAction(QString());
    fileMenu->addSeparator();
    m_exitAction = fileMenu->addAction(QString());

    QMenu* editMenu = menuBar()->addMenu(QString());
    m_glossaryAction = editMenu->addAction(QString());
    m_toneAction = editMenu->addAction(QString());
    m_historyAction = editMenu->addAction(QString());

    QMenu* viewMenu = menuBar()->addMenu(QString());
    m_autoTranslateAction = viewMenu->addAction(QString());
    m_autoTranslateAction->setCheckable(true);
    m_autoTranslateAction->setChecked(ConfigManager::instance()->boolValue(Keys::uiAutoTranslate));
    m_onTopAction = viewMenu->addAction(QString());
    m_onTopAction->setCheckable(true);
    m_onTopAction->setChecked(ConfigManager::instance()->boolValue(Keys::uiAlwaysOnTop));
    viewMenu->addSeparator();
    m_languageMenu = viewMenu->addMenu(QString());
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

    QMenu* toolsMenu = menuBar()->addMenu(QString());
    m_clipboardAction = toolsMenu->addAction(QString());
    m_clipboardAction->setCheckable(true);
    m_clipboardAction->setChecked(ConfigManager::instance()->boolValue(Keys::clipboardMonitor));
    m_hotkeySettingsAction = toolsMenu->addAction(QString());
    toolsMenu->addSeparator();
    m_settingsAction = toolsMenu->addAction(QString());

    QMenu* helpMenu = menuBar()->addMenu(QString());
    QAction* aboutAction = helpMenu->addAction(QString());

    QToolBar* toolBar = addToolBar(QString());
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_translateAction = toolBar->addAction(QString());
    m_stopAction = toolBar->addAction(QString());
    m_stopAction->setEnabled(false);
    m_translateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Return")));
    toolBar->addSeparator();
    toolBar->addAction(m_autoTranslateAction);
    toolBar->addAction(m_documentAction);
    toolBar->addAction(m_historyAction);
    toolBar->addSeparator();
    toolBar->addAction(m_settingsAction);

    connect(m_translateAction, &QAction::triggered, this, &MainWindow::translateNow);
    connect(m_stopAction, &QAction::triggered, this, [this]() { m_engine.stop(); });
    connect(m_autoTranslateAction, &QAction::toggled, this, [this](bool checked) {
        if (!m_guard)
            ConfigManager::instance()->setValue(Keys::uiAutoTranslate, checked);
    });
    connect(m_onTopAction, &QAction::toggled, this, [this](bool checked) {
        if (m_guard)
            return;
        ConfigManager::instance()->setValue(Keys::uiAlwaysOnTop, checked);
        setWindowFlag(Qt::WindowStaysOnTopHint, checked);
        if (isVisible())
            show();
    });
    connect(m_clipboardAction, &QAction::toggled, this, [this](bool checked) {
        if (!m_guard)
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
        populateToneCombo();
    });
    connect(m_toneAction, &QAction::triggered, this, [this]() {
        ToneDialog dialog(this);
        dialog.exec();
        populateToneCombo();
    });
    connect(m_historyAction, &QAction::triggered, this, &MainWindow::showHistoryDialog);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::showSettingsDialog);
    connect(m_hotkeySettingsAction, &QAction::triggered, this, [this]() {
        SettingsDialog dialog(&m_hotkey, 5, this);
        dialog.exec();
    });
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox box(QMessageBox::Information, tr("About RiipL"),
                        tr("<b>RiipL %1</b><br/>An AI-powered desktop translator.<br/>Built with Qt %2.")
                            .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion())),
                        QMessageBox::Ok, this);
        box.setInformativeText(tr("Project homepage: <a href=\"%1\">%1</a>")
                                   .arg(QString::fromLatin1(kProjectUrl)));
        box.setTextFormat(Qt::RichText);
        box.setTextInteractionFlags(Qt::TextBrowserInteraction);
        for (QLabel* label : box.findChildren<QLabel*>())
            label->setOpenExternalLinks(true);
        box.exec();
    });
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/icons/app.svg")), this);
    QMenu* menu = new QMenu();
    QAction* toggleAction = menu->addAction(QString());
    menu->addAction(m_clipboardAction);
    QAction* translateClipAction = menu->addAction(QString());
    menu->addSeparator();
    menu->addAction(m_exitAction);
    m_tray->setContextMenu(menu);
    m_tray->setToolTip(QStringLiteral("RiipL"));
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger)
            toggleVisible();
    });
    connect(toggleAction, &QAction::triggered, this, &MainWindow::toggleVisible);
    connect(translateClipAction, &QAction::triggered, this, [this]() {
        show();
        raise();
        activateWindow();
        translateClipboard();
    });
    m_tray->show();
    m_trayMenu = menu;
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
    const QString current = m_tone->currentData().toString();
    QSignalBlocker blocker(m_tone);
    m_tone->clear();
    for (const ToneItem& tone : Tones::presets())
        m_tone->addItem(Tones::presetDisplayName(tone.key, uiLanguage), tone.key);
    const QJsonArray custom = ConfigManager::instance()->value(Keys::translationCustomTones).toArray();
    for (const QJsonValue& value : custom) {
        const QJsonObject object = value.toObject();
        m_tone->addItem(object.value(QStringLiteral("name")).toString(), object.value(QStringLiteral("key")).toString());
    }
    const QString wanted = current.isEmpty() ? ConfigManager::instance()->stringValue(Keys::translationTone) : current;
    const int index = m_tone->findData(wanted);
    m_tone->setCurrentIndex(index < 0 ? 0 : index);
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

void MainWindow::pushResultSnapshot()
{
    const QString current = m_resultEdit->toPlainText();
    if (current.isEmpty())
        return;
    if (!m_resultSnapshots.isEmpty() && m_resultSnapshots.last() == current)
        return;
    m_resultSnapshots.append(current);
    while (m_resultSnapshots.size() > 30)
        m_resultSnapshots.removeFirst();
    updateUndoAction();
}

void MainWindow::undoResult()
{
    if (m_resultSnapshots.isEmpty())
        return;
    const QString previous = m_resultSnapshots.takeLast();
    m_resultEdit->setResult(previous);
    updateUndoAction();
    setStatusMessage(tr("Restored previous translation"), false);
}

void MainWindow::updateUndoAction()
{
    m_undoAction->setEnabled(!m_resultSnapshots.isEmpty());
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
    setBusy(true);
    m_engine.translate(currentContext());
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
        const int fallback = m_sourceLang->findData(QStringLiteral("en"));
        m_sourceLang->setCurrentIndex(fallback < 0 ? 0 : fallback);
    } else {
        const int newTarget = m_targetLang->findData(sourceCode);
        if (newTarget >= 0)
            m_targetLang->setCurrentIndex(newTarget);
    }
    const int newSource = m_sourceLang->findData(targetCode);
    if (newSource >= 0)
        m_sourceLang->setCurrentIndex(newSource);
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

void MainWindow::copyResult()
{
    const QString text = m_resultEdit->result();
    if (text.isEmpty())
        return;
    m_clipboardSelf = true;
    QApplication::clipboard()->setText(text);
    QTimer::singleShot(300, this, [this]() { m_clipboardSelf = false; });
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
    SettingsDialog dialog(&m_hotkey, 0, this);
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
        setWindowFlag(Qt::WindowStaysOnTopHint, ConfigManager::instance()->boolValue(key));
        if (isVisible())
            show();
    } else if (key == Keys::clipboardMonitor) {
        QSignalBlocker blocker(m_clipboardAction);
        m_clipboardAction->setChecked(ConfigManager::instance()->boolValue(key));
        applyClipboardMonitoring(ConfigManager::instance()->boolValue(key));
    } else if (key == Keys::clipboardDelayMs) {
        m_clipboardTimer->setInterval(ConfigManager::instance()->intValue(key));
    } else if (key == Keys::hotkeyEnabled || key == Keys::hotkeySequence) {
        applyHotkeyFromConfig();
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

void MainWindow::applyHotkeyFromConfig()
{
    ConfigManager* config = ConfigManager::instance();
    m_hotkey.setSequence(config->stringValue(Keys::hotkeySequence));
    if (config->boolValue(Keys::hotkeyEnabled)) {
        if (!m_hotkey.isActive() && !m_hotkey.setActive(true))
            setStatusMessage(m_hotkey.lastError(), true);
    } else {
        m_hotkey.setActive(false);
    }
}

void MainWindow::applyClipboardMonitoring(bool enabled)
{
    disconnect(QApplication::clipboard(), &QClipboard::dataChanged, this, nullptr);
    m_clipboardTimer->stop();
    if (enabled) {
        connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this]() {
            if (m_clipboardSelf)
                return;
            m_clipboardTimer->start();
        });
        m_lastClipboard = QApplication::clipboard()->text();
    }
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
    if (!clipboardText.isEmpty() && clipboardText != m_sourceEdit->toPlainText().trimmed()) {
        m_sourceEdit->setPlainText(clipboardText);
        translateNow();
    }
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

void MainWindow::retranslateUi()
{
    setWindowTitle(tr("RiipL Translator"));

    QMenu* fileMenu = menuBar()->actions().value(0)->menu();
    fileMenu->setTitle(tr("&File"));
    m_documentAction->setText(tr("Open document..."));
    m_documentAction->setShortcut(QKeySequence::Open);
    m_exportAction->setText(tr("Export translation..."));
    m_exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    m_exitAction->setText(tr("Exit"));
    m_exitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));

    QMenu* editMenu = menuBar()->actions().value(1)->menu();
    editMenu->setTitle(tr("&Edit"));
    m_glossaryAction->setText(tr("Glossary..."));
    m_glossaryAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    m_toneAction->setText(tr("Manage tones..."));
    m_historyAction->setText(tr("History..."));
    m_historyAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));

    QMenu* viewMenu = menuBar()->actions().value(2)->menu();
    viewMenu->setTitle(tr("&View"));
    m_autoTranslateAction->setText(tr("Auto translate"));
    m_onTopAction->setText(tr("Always on top"));
    m_languageMenu->setTitle(tr("Interface language"));
    if (!m_languageMenu->actions().isEmpty()) {
        m_languageMenu->actions().value(0)->setText(tr("Follow system"));
        syncLanguageMenu();
    }

    QMenu* toolsMenu = menuBar()->actions().value(3)->menu();
    toolsMenu->setTitle(tr("&Tools"));
    m_clipboardAction->setText(tr("Monitor clipboard"));
    m_hotkeySettingsAction->setText(tr("Global hotkey..."));
    m_settingsAction->setText(tr("Settings..."));

    QMenu* helpMenu = menuBar()->actions().value(4)->menu();
    helpMenu->setTitle(tr("&Help"));
    helpMenu->actions().value(0)->setText(tr("About RiipL"));

    const QList<QToolBar*> toolBars = findChildren<QToolBar*>();
    if (!toolBars.isEmpty()) {
        QToolBar* bar = toolBars.first();
        const QList<QAction*> actions = bar->actions();
        if (actions.size() >= 8) {
            actions.at(0)->setText(tr("Translate"));
            actions.at(1)->setText(tr("Stop"));
            actions.at(3)->setText(tr("Auto translate"));
            actions.at(4)->setText(tr("Document"));
            actions.at(5)->setText(tr("History"));
            actions.at(7)->setText(tr("Settings"));
        }
    }
    m_translateAction->setToolTip(tr("Translate now (Ctrl+Return)"));
    m_stopAction->setToolTip(tr("Stop translation"));
    m_undoAction->setToolTip(tr("Restore previous translation"));
    m_swapAction->setToolTip(tr("Swap languages"));
    m_clearAction->setText(tr("Clear"));
    m_pasteAction->setText(tr("Paste"));
    m_copyAction->setText(tr("Copy"));

    m_sourceEdit->setPlaceholderText(tr("Enter text to translate"));
    if (m_trayMenu) {
        m_trayMenu->actions().value(0)->setText(tr("Show/Hide window"));
        m_trayMenu->actions().value(2)->setText(tr("Translate clipboard"));
    }
}
