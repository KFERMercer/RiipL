#include "DocumentTranslationWindow.h"

#include "core/config/ConfigManager.h"
#include "core/document/DocumentCache.h"
#include "ui/widgets/AppFonts.h"
#include "ui/widgets/ThemeColors.h"
#include "ui/widgets/TranslationEdit.h"
#include "ui/widgets/WindowState.h"

#include <QAction>
#include <QByteArray>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <optional>
#include <utility>

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
    // Status text can repeat a path the user typed, so it takes no markup.
    m_statusLabel->setTextFormat(Qt::PlainText);
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
    connect(m_stopAction, &QAction::triggered, this, &DocumentTranslationWindow::stop);
    connect(m_exportButton, &QPushButton::clicked, this,
            &DocumentTranslationWindow::exportResult);
    connect(m_closeButton, &QPushButton::clicked, this, &QWidget::close);

    connect(&m_translator, &DocumentTranslator::windowTranslated, this, [this](const QString& text) {
        m_completedText = text;
        m_preview->setResult(text);
        QScrollBar* bar = m_preview->verticalScrollBar();
        bar->setValue(bar->maximum());
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
        m_preview->setResult(text);
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
    m_pathEdit->setPlaceholderText(tr("Choose a .txt file"));
    m_pathEdit->setAccessibleName(tr("Document file"));
    m_preview->setAccessibleName(tr("Translated document"));
    m_previewGroup->setTitle(tr("Preview"));
    m_browseButton->setText(tr("Browse..."));
    m_translateAction->setText(tr("Translate"));
    m_translateAction->setToolTip(tr("Translate the loaded document (Ctrl+Return)"));
    m_stopAction->setText(tr("Stop"));
    m_stopAction->setToolTip(tr("Stop translation"));
    m_exportButton->setText(tr("Export translation..."));
    m_closeButton->setText(tr("Close"));
    m_statusLabel->setText(statusText());
}

QString DocumentTranslationWindow::statusText() const
{
    switch (m_status) {
    case Status::Ready:
        return tr("Ready");
    case Status::ChooseFile:
        return tr("Choose a .txt file");
    case Status::CannotOpen:
        return tr("Cannot open file: %1").arg(m_statusArgument);
    case Status::NoContent:
        return tr("No content to translate");
    case Status::Translating:
        return tr("Translating...");
    case Status::Progress:
        return tr("Translated %1/%2 windows").arg(m_progress->value()).arg(m_progress->maximum());
    case Status::Finished:
        return tr("Translation finished");
    case Status::Failed:
        return tr("Failed windows: %1").arg(m_statusArgument);
    case Status::Stopped:
        return tr("Stopped");
    case Status::Exported:
        return tr("Exported to %1").arg(m_statusArgument);
    case Status::CannotWrite:
        return tr("Cannot write file: %1").arg(m_statusArgument);
    }
    return QString();
}

void DocumentTranslationWindow::browse()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open document"), QString(),
                                                      tr("Text files (*.txt)"));
    if (path.isEmpty())
        return;
    m_pathEdit->setText(path);
    clearResult();
}

// Drops what the last run left on screen, so a path the window has not read yet
// does not sit beside the result of another one.
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
        QMessageBox::warning(this, tr("RiipL"), tr("Cannot open file: %1").arg(path));
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
        QMessageBox::information(this, tr("RiipL"), tr("No content to translate"));
        return false;
    }
    return true;
}

void DocumentTranslationWindow::start()
{
    if (m_running)
        return;
    // The document is read here rather than on Browse, so a run always translates
    // the file the path box holds.
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
    m_translator.start(m_windows, m_contextProvider(), std::move(cache));
}

void DocumentTranslationWindow::stop()
{
    m_translator.stop();
}

void DocumentTranslationWindow::exportResult()
{
    if (m_completedText.isEmpty())
        return;
    const QFileInfo info(m_loadedPath);
    const QString suggested = info.absolutePath().isEmpty()
        ? QStringLiteral("translation.txt")
        : info.dir().filePath(info.completeBaseName() + QStringLiteral(".translated.txt"));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export translation"), suggested);
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    const QByteArray data = m_completedText.toUtf8();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(data) != data.size()
        || !file.commit()) {
        setStatus(Status::CannotWrite, path);
        QMessageBox::warning(this, tr("RiipL"), tr("Cannot write file: %1").arg(path));
        return;
    }
    setStatus(Status::Exported, path);
}

// A run leaves the preview on the document accepted so far, which keeps the text
// of the windows the run did not reach. The pane keeps whatever it shows, so a
// stop does not wipe the lines already translated.
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
    if (running)
        m_exportButton->setEnabled(false);
}

void DocumentTranslationWindow::setStatus(Status status, const QString& argument)
{
    m_status = status;
    m_statusArgument = argument;
    m_statusLabel->setText(statusText());
    const bool error = status == Status::CannotOpen || status == Status::Failed
        || status == Status::CannotWrite;
    if (error)
        ThemeColors::setTextColor(m_statusLabel, ThemeColors::errorText(m_statusLabel));
    else
        m_statusLabel->setPalette(QPalette());
}
