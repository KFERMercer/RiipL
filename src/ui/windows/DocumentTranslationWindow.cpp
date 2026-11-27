#include "DocumentTranslationWindow.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/config/Preset.h"
#include "core/document/DocumentCache.h"
#include "core/translation/Language.h"
#include "ui/dialogs/ContextPresetDialog.h"
#include "ui/widgets/AppFonts.h"
#include "ui/widgets/AppIcons.h"
#include "ui/widgets/ConfigEditors.h"
#include "ui/widgets/StatusPresenter.h"
#include "ui/widgets/TranslationEdit.h"
#include "ui/widgets/WindowState.h"

#include <QAction>
#include <QByteArray>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QStyle>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <chrono>
#include <exception>
#include <optional>
#include <utility>

namespace {

// Windows reported within one interval go into the preview as one insertion.
constexpr std::chrono::milliseconds kPreviewInterval{50};

QVector<Preset> storedApiPresets()
{
    return Presets::fromJson(ConfigManager::instance()->value(Keys::apiPresets).toArray());
}

QFrame* verticalSeparator(QWidget* parent)
{
    auto* separator = new QFrame(parent);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Sunken);
    return separator;
}

// Reads the document and splits it into windows; runs off the interface thread,
// since a long document would hold it for seconds. The text comes back whole only
// when the preview is to show it.
LoadedDocument loadDocumentFile(const QString& path, int wordLimit, int lineLimit,
                                bool keepText)
{
    LoadedDocument loaded;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        loaded.error = path;
        return loaded;
    }
    const QByteArray bytes = file.readAll();
    // A read that stopped short hands back what it read, which is no document.
    if (file.error() != QFileDevice::NoError) {
        loaded.error = path;
        return loaded;
    }
    loaded.path = path;
    loaded.windows = DocumentSegmenter::partition(QString::fromUtf8(bytes), wordLimit, lineLimit);
    if (keepText && !loaded.windows.isEmpty())
        loaded.text = DocumentSegmenter::assemble(loaded.windows, {});
    return loaded;
}

}

DocumentTranslationWindow::DocumentTranslationWindow(ContextProvider context)
    : QWidget(nullptr)
    , m_contextProvider(std::move(context))
{
    // Closing the window must not count as the application's last window while the
    // main window sits in the tray.
    setAttribute(Qt::WA_QuitOnClose, false);

    auto* layout = new QVBoxLayout(this);

    auto* fileRow = new QHBoxLayout();
    m_pathEdit = new QLineEdit(this);
    m_browseButton = new QPushButton(this);
    fileRow->addWidget(m_pathEdit, 1);
    fileRow->addWidget(m_browseButton);
    layout->addLayout(fileRow);

    auto* optionsRow = new QHBoxLayout();
    m_sourceLang = new QComboBox(this);
    m_sourceLang->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_arrow = new QLabel(this);
    m_targetLang = new QComboBox(this);
    m_targetLang->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_tone = new QComboBox(this);
    m_tone->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_contextAction = new QAction(this);
    auto* contextButton = new QToolButton(this);
    contextButton->setDefaultAction(m_contextAction);
    contextButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_apiPreset = new QComboBox(this);
    m_apiPreset->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    optionsRow->addWidget(m_sourceLang);
    optionsRow->addWidget(m_arrow);
    optionsRow->addWidget(m_targetLang);
    optionsRow->addWidget(verticalSeparator(this));
    optionsRow->addWidget(m_tone);
    optionsRow->addWidget(verticalSeparator(this));
    optionsRow->addWidget(contextButton);
    optionsRow->addStretch(1);
    optionsRow->addWidget(m_apiPreset);
    layout->addLayout(optionsRow);
    updateArrowIcon();

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    layout->addWidget(m_progress);

    m_preview = new TranslationEdit(this);
    m_preview->setFont(AppFonts::editorFont());
    m_preview->setWordSelectionEnabled(false);

    m_previewGroup = new QGroupBox(this);
    auto* previewLayout = new QVBoxLayout(m_previewGroup);
    previewLayout->addWidget(m_preview);
    layout->addWidget(m_previewGroup, 1);

    m_statusLabel = new QLabel(this);
    m_statusPresenter = std::make_unique<StatusPresenter>(m_statusLabel);
    layout->addWidget(m_statusLabel);

    auto* buttonRow = new QHBoxLayout();

    m_translateAction = new QAction(this);
    m_translateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Return")));
    auto* translateButton = new QToolButton(this);
    translateButton->setDefaultAction(m_translateAction);
    translateButton->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_stopAction = new QAction(this);
    m_stopAction->setEnabled(false);
    auto* stopButton = new QToolButton(this);
    stopButton->setDefaultAction(m_stopAction);
    stopButton->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_exportButton = new QPushButton(this);
    m_exportButton->setEnabled(false);
    auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);

    buttonRow->addWidget(translateButton);
    buttonRow->addWidget(stopButton);
    buttonRow->addWidget(m_exportButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(closeButtons);
    layout->addLayout(buttonRow);

    connect(m_browseButton, &QPushButton::clicked, this, &DocumentTranslationWindow::browse);
    connect(m_translateAction, &QAction::triggered, this, &DocumentTranslationWindow::start);
    connect(m_stopAction, &QAction::triggered, this,
            &DocumentTranslationWindow::stopTranslation);
    connect(m_exportButton, &QPushButton::clicked, this,
            &DocumentTranslationWindow::exportResult);
    connect(closeButtons, &QDialogButtonBox::rejected, this, &QWidget::close);
    connect(m_contextAction, &QAction::triggered, this,
            [this]() { ContextPresetDialog::manage(this); });

    connect(m_apiPreset, &QComboBox::currentIndexChanged, this, [this](int index) {
        const QVector<Preset> presets = storedApiPresets();
        if (index >= 0 && index < presets.size())
            Presets::apply(presets.at(index), Keys::apiPresetFields());
    });
    connect(m_sourceLang, &QComboBox::currentIndexChanged, this, [this](int index) {
        ConfigManager::instance()->setValue(Keys::translationSourceLang,
                                            m_sourceLang->itemData(index).toString());
    });
    connect(m_targetLang, &QComboBox::currentIndexChanged, this, [this](int index) {
        ConfigManager::instance()->setValue(Keys::translationTargetLang,
                                            m_targetLang->itemData(index).toString());
    });
    connect(m_tone, &QComboBox::currentIndexChanged, this, [this](int index) {
        ConfigManager::instance()->setValue(Keys::translationTone,
                                            m_tone->itemData(index).toString());
    });
    connect(ConfigManager::instance(), &ConfigManager::changed, this,
            &DocumentTranslationWindow::onConfigChanged);
    connect(&m_loadWatcher, &QFutureWatcher<LoadedDocument>::finished, this,
            &DocumentTranslationWindow::handleLoaded);

    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(kPreviewInterval);
    connect(&m_previewTimer, &QTimer::timeout, this, &DocumentTranslationWindow::flushPreview);

    connect(&m_translator, &DocumentTranslator::windowTranslated, this,
            [this](int index, const QString& text) {
                if (!m_streaming) {
                    // The document as it was loaded stands until the first window is
                    // reported; the translation replaces it from here on.
                    m_streaming = true;
                    m_preview->setResult(QString());
                }
                m_pendingText[index] = text;
                if (!m_previewTimer.isActive())
                    m_previewTimer.start();
            });
    connect(&m_translator, &DocumentTranslator::progressChanged, this,
            [this](int completed, int total) {
                m_progress->setRange(0, total);
                m_progress->setValue(completed);
                if (completed > 0)
                    setStatus(Status::Progress);
            });
    connect(&m_translator, &DocumentTranslator::finished, this, [this](const QString& text) {
        flushPreview();
        m_completedText = text;
        endRun();
        setStatus(Status::Finished);
    });
    connect(&m_translator, &DocumentTranslator::failed, this,
            [this](const QStringList& samples) {
                flushPreview();
                endRun();
                setStatus(Status::Failed, samples.join(QStringLiteral(", ")));
            });
    connect(&m_translator, &DocumentTranslator::stopped, this, [this]() {
        flushPreview();
        endRun();
        setStatus(Status::Stopped);
    });

    WindowState::track(this, WindowState::Id::document);
    retranslateUi();
}

DocumentTranslationWindow::~DocumentTranslationWindow() = default;

void DocumentTranslationWindow::closeEvent(QCloseEvent* event)
{
    stopTranslation();
    QWidget::closeEvent(event);
}

void DocumentTranslationWindow::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    else if (event->type() == QEvent::ApplicationFontChange)
        m_preview->setFont(AppFonts::editorFont());
    else if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange
             || event->type() == QEvent::ApplicationPaletteChange
             || event->type() == QEvent::DevicePixelRatioChange)
        updateArrowIcon();
}

void DocumentTranslationWindow::retranslateUi()
{
    setWindowTitle(tr("Document translation"));
    m_pathEdit->setPlaceholderText(tr("Choose file"));
    m_pathEdit->setAccessibleName(tr("Document file"));
    m_preview->setAccessibleName(tr("Translated document"));
    m_previewGroup->setTitle(tr("Preview"));
    m_browseButton->setText(tr("Browse"));
    m_translateAction->setText(tr("Translate"));
    m_translateAction->setToolTip(tr("Start translation"));
    m_stopAction->setText(tr("Stop"));
    m_stopAction->setToolTip(tr("Stop translation"));
    m_exportButton->setText(tr("Export translation"));
    m_contextAction->setText(tr("Context"));
    m_contextAction->setToolTip(tr("Manage context presets"));
    m_sourceLang->setAccessibleName(tr("Source language"));
    m_sourceLang->setToolTip(tr("Source language"));
    m_targetLang->setAccessibleName(tr("Target language"));
    m_targetLang->setToolTip(tr("Target language"));
    m_tone->setAccessibleName(tr("Tone"));
    m_tone->setToolTip(tr("Tone"));
    m_apiPreset->setAccessibleName(tr("API preset"));
    m_apiPreset->setToolTip(tr("API preset"));
    m_statusPresenter->refresh();

    populateLanguageCombo(m_sourceLang, true, Keys::translationSourceLang);
    populateLanguageCombo(m_targetLang, false, Keys::translationTargetLang);
    populateTones();
    populateApiPresets();
}

void DocumentTranslationWindow::updateArrowIcon()
{
    const int side = style()->pixelMetric(QStyle::PM_SmallIconSize);
    m_arrow->setPixmap(AppIcons::arrowRight().pixmap(QSize(side, side), devicePixelRatioF()));
}

void DocumentTranslationWindow::populateApiPresets()
{
    const QVector<Preset> presets = storedApiPresets();
    {
        QSignalBlocker blocker(m_apiPreset);
        m_apiPreset->clear();
        for (const Preset& preset : presets)
            m_apiPreset->addItem(preset.name, preset.name);
        m_apiPreset->setPlaceholderText(presets.isEmpty() ? tr("No presets") : tr("Custom"));
    }
    syncApiPreset();
    updateOptionsEnabled();
}

void DocumentTranslationWindow::syncApiPreset()
{
    QSignalBlocker blocker(m_apiPreset);
    const QStringList fields = Keys::apiPresetFields();
    m_apiPreset->setCurrentIndex(
        Presets::matchValues(storedApiPresets(), Presets::capture(fields), fields));
}

void DocumentTranslationWindow::populateTones()
{
    ConfigManager* config = ConfigManager::instance();
    QSignalBlocker blocker(m_tone);
    m_tone->clear();
    for (const std::pair<QString, QString>& item
         : toneItems(config->value(Keys::translationCustomTones).toArray())) {
        m_tone->addItem(item.first, item.second);
    }
    selectComboItem(m_tone, config->stringValue(Keys::translationTone),
                    Defaults::value(Keys::translationTone).toString());
}

void DocumentTranslationWindow::populateLanguageCombo(QComboBox* combo, bool includeAuto,
                                                     const QString& key)
{
    QSignalBlocker blocker(combo);
    combo->clear();
    const QVector<CustomLanguage> custom = Languages::customFromJson(
        ConfigManager::instance()->value(Keys::translationCustomLanguages).toArray());
    for (const std::pair<QString, QString>& item : languageItems(includeAuto, custom))
        combo->addItem(item.first, item.second);
    selectComboItem(combo, ConfigManager::instance()->stringValue(key),
                    Defaults::value(key).toString());
}

void DocumentTranslationWindow::updateOptionsEnabled()
{
    const bool editable = !m_running && !m_loading;
    m_sourceLang->setEnabled(editable);
    m_targetLang->setEnabled(editable);
    m_contextAction->setEnabled(editable);
    m_tone->setEnabled(editable);
    m_apiPreset->setEnabled(editable && m_apiPreset->count() > 0);
}

void DocumentTranslationWindow::onConfigChanged(const QString& key)
{
    if (key == Keys::translationSourceLang || key == Keys::translationTargetLang) {
        QComboBox* combo = key == Keys::translationSourceLang ? m_sourceLang : m_targetLang;
        QSignalBlocker blocker(combo);
        selectComboItem(combo, ConfigManager::instance()->stringValue(key),
                        Defaults::value(key).toString());
    } else if (key == Keys::translationCustomLanguages) {
        populateLanguageCombo(m_sourceLang, true, Keys::translationSourceLang);
        populateLanguageCombo(m_targetLang, false, Keys::translationTargetLang);
    } else if (key == Keys::translationTone || key == Keys::translationCustomTones) {
        populateTones();
    } else if (key == Keys::apiPresets) {
        populateApiPresets();
    } else if (Keys::apiPresetFields().contains(key)) {
        syncApiPreset();
    }
}

QString DocumentTranslationWindow::statusText(Status status, const QString& argument) const
{
    switch (status) {
    case Status::Ready:
        return tr("Ready");
    case Status::ChooseFile:
        return tr("Choose file");
    case Status::Loading:
        return tr("Loading...");
    case Status::CannotOpen:
        return tr("Cannot open file: %1").arg(argument);
    case Status::NoContent:
        return tr("No content to translate");
    case Status::Translating:
        return tr("Translating...");
    case Status::Progress:
        return tr("Translated %1/%2 windows").arg(m_progress->value()).arg(m_progress->maximum());
    case Status::Finished:
        return tr("Translation finished");
    case Status::Failed:
        return tr("Failed windows: %1").arg(argument);
    case Status::Stopped:
        return tr("Stopped");
    case Status::Exported:
        return tr("Exported to %1").arg(argument);
    case Status::CannotWrite:
        return tr("Cannot write file: %1").arg(argument);
    }
    return QString();
}

bool DocumentTranslationWindow::statusIsError(Status status)
{
    return status == Status::CannotOpen || status == Status::Failed
        || status == Status::CannotWrite;
}

void DocumentTranslationWindow::browse()
{
    QFileDialog dialog(this, tr("Document translation"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setMimeTypeFilters(
        {QStringLiteral("text/plain"), QStringLiteral("application/octet-stream")});
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString path = dialog.selectedFiles().value(0);
    if (path.isEmpty())
        return;
    m_pathEdit->setText(path);
    loadDocument(false);
}

void DocumentTranslationWindow::clearResult()
{
    m_previewTimer.stop();
    m_pendingText.clear();
    m_placedLength.clear();
    m_placedHighest = -1;
    m_completedText.clear();
    m_streaming = false;
    m_preview->setResult(QString());
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_exportButton->setEnabled(false);
    setStatus(Status::Ready);
}

// What is on screen belongs to the path the box held before, so it is dropped
// before the file is read.
void DocumentTranslationWindow::loadDocument(bool thenTranslate)
{
    if (m_loading)
        return;
    const QString path = m_pathEdit->text().trimmed();
    m_loadedPath.clear();
    m_windows.clear();
    // A run leaves the preview as it stands until its first window is reported.
    if (thenTranslate) {
        m_previewTimer.stop();
        m_pendingText.clear();
        m_placedLength.assign(m_windows.size(), 0);
        m_placedHighest = -1;
        m_completedText.clear();
        m_streaming = false;
        m_exportButton->setEnabled(false);
    } else {
        clearResult();
    }

    if (path.isEmpty()) {
        setStatus(Status::ChooseFile);
        return;
    }

    // The limits are read here, since the worker reads neither the configuration
    // nor the interface.
    ConfigManager* config = ConfigManager::instance();
    const int wordLimit = config->intValue(Keys::documentWindowWords);
    const int lineLimit = config->intValue(Keys::documentWindowLines);

    m_startAfterLoad = thenTranslate;
    m_loadDropped = false;
    setLoading(true);
    setStatus(Status::Loading);
    m_loadWatcher.setFuture(QtConcurrent::run([path, wordLimit, lineLimit, thenTranslate]() {
        return loadDocumentFile(path, wordLimit, lineLimit, !thenTranslate);
    }));
}

void DocumentTranslationWindow::handleLoaded()
{
    setLoading(false);
    // The run the document was read for is gone, so the document is not wanted.
    if (std::exchange(m_loadDropped, false)) {
        m_startAfterLoad = false;
        setStatus(Status::Ready);
        return;
    }

    LoadedDocument loaded;
    try {
        loaded = m_loadWatcher.result();
    } catch (const std::exception&) {
        setStatus(Status::CannotOpen, m_pathEdit->text().trimmed());
        QMessageBox::warning(
            this, QCoreApplication::applicationName(),
            tr("Cannot open file: %1").arg(m_pathEdit->text().trimmed()));
        return;
    }
    const bool thenTranslate = m_startAfterLoad;
    m_startAfterLoad = false;

    if (!loaded.error.isEmpty()) {
        setStatus(Status::CannotOpen, loaded.error);
        QMessageBox::warning(this, QCoreApplication::applicationName(),
                             tr("Cannot open file: %1").arg(loaded.error));
        return;
    }
    if (loaded.windows.isEmpty()) {
        setStatus(Status::NoContent);
        QMessageBox::information(this, QCoreApplication::applicationName(),
                                 tr("No content to translate"));
        return;
    }

    m_loadedPath = loaded.path;
    m_windows = loaded.windows;
    if (thenTranslate) {
        startTranslation();
        return;
    }
    // The file reads as it is until a run replaces it with the translation.
    m_preview->setResult(loaded.text);
    setStatus(Status::Ready);
}

void DocumentTranslationWindow::start()
{
    if (m_running || m_loading)
        return;
    // Read here, so a run always translates the file the path box holds.
    loadDocument(true);
}

void DocumentTranslationWindow::startTranslation()
{
    ConfigManager* config = ConfigManager::instance();
    std::optional<DocumentCache> cache = std::nullopt;
    if (config->boolValue(Keys::documentCacheEnabled))
        cache.emplace();

    m_previewTimer.stop();
    m_pendingText.clear();
    m_placedLength.assign(m_windows.size(), 0);
    m_placedHighest = -1;
    m_completedText.clear();
    m_streaming = false;

    m_progress->setRange(0, m_windows.size());
    m_progress->setValue(0);
    setRunning(true);
    setStatus(Status::Translating);
    TranslationContext context = m_contextProvider();
    context.sourceLang = m_sourceLang->currentData().toString();
    context.targetLang = m_targetLang->currentData().toString();
    context.tone = m_tone->currentData().toString();
    m_resultLang = context.targetLang;
    m_translator.start(m_windows, context, std::move(cache));
}

void DocumentTranslationWindow::stopTranslation()
{
    m_translator.stop();
    if (m_loading) {
        // The load belongs to the run that is ending, so what it reads is dropped.
        m_loadDropped = true;
        m_startAfterLoad = false;
    }
}

bool DocumentTranslationWindow::hasTranslatedWindow() const
{
    return m_progress->value() > 0;
}

void DocumentTranslationWindow::exportResult()
{
    if (!hasTranslatedWindow())
        return;
    const QFileInfo info(m_loadedPath);
    const QString suffix = info.suffix().isEmpty() ? QStringLiteral("txt") : info.suffix();
    // A custom language carries no BCP-47 code, so its key names the export.
    const QString name = QStringLiteral("%1.translated.%2.%3")
                             .arg(info.completeBaseName(), m_resultLang, suffix);
    const QString path
        = QFileDialog::getSaveFileName(this, tr("Export translation"), info.dir().filePath(name));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    // A run that ended early still exports whole: the windows it left out read as
    // their source text.
    const QString text = m_completedText.isEmpty() ? m_translator.document() : m_completedText;
    const QByteArray data = text.toUtf8();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(data) != data.size()
        || !file.commit()) {
        setStatus(Status::CannotWrite, path);
        QMessageBox::warning(this, QCoreApplication::applicationName(),
                             tr("Cannot write file: %1").arg(path));
        return;
    }
    setStatus(Status::Exported, path);
}

// The preview already holds the windows accepted so far, so a stop does not wipe
// the lines translated.
void DocumentTranslationWindow::endRun()
{
    setRunning(false);
    m_exportButton->setEnabled(hasTranslatedWindow());
}

// A window goes in ahead of the ones placed after it: the gap is as wide as the
// requests in flight.
void DocumentTranslationWindow::flushPreview()
{
    m_previewTimer.stop();
    if (m_pendingText.empty())
        return;

    // Keep the preview at its end only while the reader stays there.
    const bool follow = m_preview->isScrolledToBottom();

    QTextDocument* document = m_preview->document();
    // The windows that close no gap are written in one piece, since each insertion
    // into a long document is what a run pays for.
    QString piece;
    const auto writePiece = [&piece, document]() {
        if (piece.isEmpty())
            return;
        QTextCursor cursor(document);
        cursor.setPosition(document->characterCount() - 1);
        cursor.insertText(piece);
        piece.clear();
    };
    for (auto pending = m_pendingText.begin(); pending != m_pendingText.end(); ++pending) {
        const int index = pending->first;
        // A paragraph break is one position, however many characters write it.
        QString text = pending->second;
        if (text.contains(QLatin1Char('\r'))) {
            text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
            text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        }
        qsizetype tail = 0;
        for (int after = index + 1; after <= m_placedHighest; ++after)
            tail += m_placedLength.at(after);
        if (tail == 0) {
            piece += text;
        } else {
            QTextCursor cursor(document);
            cursor.setPosition(document->characterCount() - 1 - tail);
            cursor.insertText(text);
        }
        m_placedLength[index] = text.size();
        m_placedHighest = (std::max)(m_placedHighest, index);
    }
    writePiece();
    m_pendingText.clear();
    if (follow)
        m_preview->scrollToBottom();
}

void DocumentTranslationWindow::setRunning(bool running)
{
    m_running = running;
    if (running)
        m_exportButton->setEnabled(false);
    updateActions();
}

void DocumentTranslationWindow::setLoading(bool loading)
{
    m_loading = loading;
    updateActions();
}

void DocumentTranslationWindow::updateActions()
{
    const bool idle = !m_running && !m_loading;
    m_translateAction->setEnabled(idle);
    m_stopAction->setEnabled(m_running);
    m_pathEdit->setEnabled(idle);
    m_browseButton->setEnabled(idle);
    updateOptionsEnabled();
}

void DocumentTranslationWindow::setStatus(Status status, const QString& argument)
{
    auto text = [this, status, argument]() { return statusText(status, argument); };
    if (statusIsError(status))
        m_statusPresenter->showFailure(std::move(text));
    else
        m_statusPresenter->show(std::move(text));
}
