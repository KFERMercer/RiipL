#include "DocumentTranslationWindow.h"

#include "core/config/Preset.h"
#include "core/config/ConfigManager.h"
#include "core/document/DocumentCache.h"
#include "ui/dialogs/ContextPresetDialog.h"
#include "ui/widgets/AppFonts.h"
#include "ui/widgets/ConfigEditors.h"
#include "ui/widgets/StatusPresenter.h"
#include "ui/widgets/TranslationEdit.h"
#include "ui/widgets/WindowState.h"

#include <QAction>
#include <QByteArray>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
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
#include <QToolButton>
#include <QVBoxLayout>

#include <optional>
#include <utility>

namespace {

QVector<Preset> storedApiPresets()
{
    return Presets::fromJson(ConfigManager::instance()->value(Keys::apiPresets).toArray());
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
    m_contextAction = new QAction(this);
    auto* contextButton = new QToolButton(this);
    contextButton->setDefaultAction(m_contextAction);
    contextButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* optionsSeparator = new QFrame(this);
    optionsSeparator->setFrameShape(QFrame::VLine);
    optionsSeparator->setFrameShadow(QFrame::Sunken);
    m_apiPreset = new QComboBox(this);
    m_tone = new QComboBox(this);
    m_targetLang = new QComboBox(this);
    optionsRow->addWidget(contextButton);
    optionsRow->addWidget(optionsSeparator);
    optionsRow->addWidget(m_apiPreset, 1);
    optionsRow->addWidget(m_tone, 1);
    optionsRow->addWidget(m_targetLang, 1);
    layout->addLayout(optionsRow);

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
    m_closeButton = new QPushButton(this);

    buttonRow->addWidget(translateButton);
    buttonRow->addWidget(stopButton);
    buttonRow->addWidget(m_exportButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_closeButton);
    layout->addLayout(buttonRow);

    connect(m_browseButton, &QPushButton::clicked, this, &DocumentTranslationWindow::browse);
    connect(m_translateAction, &QAction::triggered, this, &DocumentTranslationWindow::start);
    connect(m_stopAction, &QAction::triggered, this,
            &DocumentTranslationWindow::stopTranslation);
    connect(m_exportButton, &QPushButton::clicked, this,
            &DocumentTranslationWindow::exportResult);
    connect(m_closeButton, &QPushButton::clicked, this, &QWidget::close);
    connect(m_contextAction, &QAction::triggered, this,
            [this]() { ContextPresetDialog::manage(this); });

    connect(m_apiPreset, &QComboBox::currentIndexChanged, this, [this](int index) {
        const QVector<Preset> presets = storedApiPresets();
        if (index >= 0 && index < presets.size())
            Presets::apply(presets.at(index), Keys::apiPresetFields());
    });
    connect(m_tone, &QComboBox::currentIndexChanged, this, [this](int index) {
        ConfigManager::instance()->setValue(Keys::translationTone,
                                            m_tone->itemData(index).toString());
    });
    connect(m_targetLang, &QComboBox::currentIndexChanged, this, [this](int index) {
        ConfigManager::instance()->setValue(Keys::translationTargetLang,
                                            m_targetLang->itemData(index).toString());
    });
    connect(ConfigManager::instance(), &ConfigManager::changed, this,
            &DocumentTranslationWindow::onConfigChanged);

    connect(&m_translator, &DocumentTranslator::windowTranslated, this, [this](const QString& text) {
        m_completedText = text;
        m_preview->updateResult(text);
    });
    connect(&m_translator, &DocumentTranslator::progressChanged, this,
            [this](int completed, int total) {
                m_progress->setRange(0, total);
                m_progress->setValue(completed);
                if (completed > 0)
                    setStatus(Status::Progress);
            });
    connect(&m_translator, &DocumentTranslator::finished, this, [this](const QString& text) {
        m_completedText = text;
        m_preview->updateResult(text);
        endRun();
        setStatus(Status::Finished);
    });
    connect(&m_translator, &DocumentTranslator::failed, this,
            [this](const QStringList& samples) {
                endRun();
                setStatus(Status::Failed, samples.join(QStringLiteral(", ")));
            });
    connect(&m_translator, &DocumentTranslator::stopped, this, [this]() {
        endRun();
        setStatus(Status::Stopped);
    });

    WindowState::track(this, WindowState::Id::document);
    retranslateUi();
}

DocumentTranslationWindow::~DocumentTranslationWindow() = default;

void DocumentTranslationWindow::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    else if (event->type() == QEvent::ApplicationFontChange)
        m_preview->setFont(AppFonts::editorFont());
}

void DocumentTranslationWindow::retranslateUi()
{
    setWindowTitle(tr("Document translation"));
    m_pathEdit->setPlaceholderText(tr("Choose file"));
    m_pathEdit->setAccessibleName(tr("Document file"));
    m_preview->setAccessibleName(tr("Translated document"));
    m_previewGroup->setTitle(tr("Preview"));
    m_browseButton->setText(tr("Browse..."));
    m_translateAction->setText(tr("Translate"));
    m_stopAction->setText(tr("Stop"));
    m_stopAction->setToolTip(tr("Stop translation"));
    m_exportButton->setText(tr("Export translation..."));
    m_closeButton->setText(tr("Close"));
    m_contextAction->setText(tr("Context"));
    m_contextAction->setToolTip(tr("Manage context presets"));
    m_apiPreset->setAccessibleName(tr("API preset"));
    m_tone->setAccessibleName(tr("Tone"));
    m_targetLang->setAccessibleName(tr("Target language"));
    m_statusPresenter->refresh();

    populateApiPresets();
    populateTones();
    populateTargetLanguages();
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
    selectComboItem(m_tone, config->stringValue(Keys::translationTone));
}

void DocumentTranslationWindow::populateTargetLanguages()
{
    ConfigManager* config = ConfigManager::instance();
    QSignalBlocker blocker(m_targetLang);
    m_targetLang->clear();
    for (const std::pair<QString, QString>& item : languageItems(false))
        m_targetLang->addItem(item.first, item.second);
    selectComboItem(m_targetLang, config->stringValue(Keys::translationTargetLang));
}

void DocumentTranslationWindow::updateOptionsEnabled()
{
    const bool editable = !m_running;
    m_contextAction->setEnabled(editable);
    m_apiPreset->setEnabled(editable && m_apiPreset->count() > 0);
    m_tone->setEnabled(editable);
    m_targetLang->setEnabled(editable);
}

void DocumentTranslationWindow::onConfigChanged(const QString& key)
{
    if (key == Keys::translationTargetLang) {
        QSignalBlocker blocker(m_targetLang);
        selectComboItem(m_targetLang, ConfigManager::instance()->stringValue(key));
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
    clearResult();
}

void DocumentTranslationWindow::clearResult()
{
    m_completedText.clear();
    m_preview->setResult(QString());
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_exportButton->setEnabled(false);
    setStatus(Status::Ready);
}

bool DocumentTranslationWindow::loadFile()
{
    // What is on screen belongs to the path the box held before, so it is dropped
    // before the file is read.
    const QString path = m_pathEdit->text().trimmed();
    m_loadedPath.clear();
    m_windows.clear();
    clearResult();

    if (path.isEmpty()) {
        setStatus(Status::ChooseFile);
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(Status::CannotOpen, path);
        QMessageBox::warning(this, QCoreApplication::applicationName(),
                             tr("Cannot open file: %1").arg(path));
        return false;
    }

    const QString content = QString::fromUtf8(file.readAll());
    m_loadedPath = path;
    ConfigManager* config = ConfigManager::instance();
    m_windows = DocumentSegmenter::partition(content,
                                             config->intValue(Keys::documentWindowWords),
                                             config->intValue(Keys::documentWindowLines));
    if (m_windows.isEmpty()) {
        setStatus(Status::NoContent);
        QMessageBox::information(this, QCoreApplication::applicationName(),
                                 tr("No content to translate"));
        return false;
    }
    return true;
}

void DocumentTranslationWindow::start()
{
    if (m_running)
        return;
    // Read here, so a run always translates the file the path box holds.
    if (!loadFile())
        return;

    ConfigManager* config = ConfigManager::instance();
    std::optional<DocumentCache> cache = std::nullopt;
    if (config->boolValue(Keys::documentCacheEnabled))
        cache.emplace();

    m_progress->setRange(0, m_windows.size());
    m_progress->setValue(0);
    setRunning(true);
    setStatus(Status::Translating);
    TranslationContext context = m_contextProvider();
    context.targetLang = m_targetLang->currentData().toString();
    context.tone = m_tone->currentData().toString();
    m_resultLang = context.targetLang;
    m_translator.start(m_windows, context, std::move(cache));
}

void DocumentTranslationWindow::stopTranslation()
{
    m_translator.stop();
}

void DocumentTranslationWindow::exportResult()
{
    if (m_completedText.isEmpty())
        return;
    const QFileInfo info(m_loadedPath);
    const QString suffix = info.suffix().isEmpty() ? QStringLiteral("txt") : info.suffix();
    const QString name = QStringLiteral("%1.translated.%2.%3")
                             .arg(info.completeBaseName(), m_resultLang, suffix);
    const QString path
        = QFileDialog::getSaveFileName(this, tr("Export translation"), info.dir().filePath(name));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    const QByteArray data = m_completedText.toUtf8();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(data) != data.size()
        || !file.commit()) {
        setStatus(Status::CannotWrite, path);
        QMessageBox::warning(this, QCoreApplication::applicationName(),
                             tr("Cannot write file: %1").arg(path));
        return;
    }
    setStatus(Status::Exported, path);
}

// The preview already holds the document accepted so far, so a stop does not wipe
// the lines translated.
void DocumentTranslationWindow::endRun()
{
    setRunning(false);
    m_exportButton->setEnabled(!m_completedText.isEmpty());
}

void DocumentTranslationWindow::setRunning(bool running)
{
    m_running = running;
    m_translateAction->setEnabled(!running);
    m_stopAction->setEnabled(running);
    m_pathEdit->setEnabled(!running);
    m_browseButton->setEnabled(!running);
    updateOptionsEnabled();
    if (running)
        m_exportButton->setEnabled(false);
}

void DocumentTranslationWindow::setStatus(Status status, const QString& argument)
{
    auto text = [this, status, argument]() { return statusText(status, argument); };
    if (statusIsError(status))
        m_statusPresenter->showFailure(std::move(text));
    else
        m_statusPresenter->show(std::move(text));
}
