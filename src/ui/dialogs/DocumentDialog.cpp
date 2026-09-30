#include "DocumentDialog.h"

#include "ui/widgets/AppFonts.h"
#include "ui/widgets/ThemeColors.h"
#include "ui/widgets/TranslationEdit.h"
#include "ui/widgets/WindowState.h"

#include <QAction>
#include <QByteArray>
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
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

DocumentDialog::DocumentDialog(const TranslationContext& baseContext, QWidget* parent)
    : QDialog(parent)
    , m_baseContext(baseContext)
{
    setWindowTitle(tr("Document translation"));

    auto* layout = new QVBoxLayout(this);

    auto* fileRow = new QHBoxLayout();
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText(tr("Choose a .txt file"));
    m_pathEdit->setAccessibleName(tr("Document file"));
    m_browseButton = new QPushButton(tr("Browse..."), this);
    m_browseButton->setAutoDefault(false);
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
    m_preview->setAccessibleName(tr("Translated document"));

    auto* previewGroup = new QGroupBox(tr("Preview"), this);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    previewLayout->addWidget(m_preview);
    layout->addWidget(previewGroup, 1);

    m_status = new QLabel(tr("Ready"), this);
    layout->addWidget(m_status);

    auto* buttonRow = new QHBoxLayout();

    m_translateAction = new QAction(tr("Translate"), this);
    m_translateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Return")));
    m_translateAction->setToolTip(tr("Translate the loaded document (Ctrl+Return)"));
    auto* translateButton = new QToolButton(this);
    translateButton->setDefaultAction(m_translateAction);
    translateButton->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_stopAction = new QAction(tr("Stop"), this);
    m_stopAction->setToolTip(tr("Stop translation"));
    m_stopAction->setEnabled(false);
    auto* stopButton = new QToolButton(this);
    stopButton->setDefaultAction(m_stopAction);
    stopButton->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_exportButton = new QPushButton(tr("Export translation..."), this);
    m_exportButton->setEnabled(false);
    m_exportButton->setAutoDefault(false);
    auto* closeButton = new QPushButton(tr("Close"), this);
    closeButton->setAutoDefault(false);

    buttonRow->addWidget(translateButton);
    buttonRow->addWidget(stopButton);
    buttonRow->addWidget(m_exportButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    connect(m_browseButton, &QPushButton::clicked, this, &DocumentDialog::browse);
    connect(m_translateAction, &QAction::triggered, this, &DocumentDialog::start);
    connect(m_stopAction, &QAction::triggered, this, &DocumentDialog::stop);
    connect(m_exportButton, &QPushButton::clicked, this, &DocumentDialog::exportResult);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    connect(&m_translator, &DocumentTranslator::windowStreamed, this, [this](const QString& piece) {
        // The finished document separates two windows with a newline, so the
        // streamed text follows one rather than running into the last line.
        if (!m_streaming) {
            m_streaming = true;
            if (!m_completedText.isEmpty())
                m_preview->appendResult(QStringLiteral("\n"));
        }
        m_preview->appendResult(piece);
        QScrollBar* bar = m_preview->verticalScrollBar();
        bar->setValue(bar->maximum());
    });
    connect(&m_translator, &DocumentTranslator::windowRestarted, this, [this]() {
        m_streaming = false;
        m_preview->setResult(m_completedText);
    });
    connect(&m_translator, &DocumentTranslator::windowTranslated, this, [this](const QString& text) {
        m_streaming = false;
        m_completedText = text;
        m_preview->setResult(text);
    });
    connect(&m_translator, &DocumentTranslator::progressChanged, this,
            [this](int completed, int total) {
                m_progress->setRange(0, total);
                m_progress->setValue(completed);
                if (completed > 0)
                    setStatus(tr("Translated %1/%2 windows").arg(completed).arg(total));
            });
    connect(&m_translator, &DocumentTranslator::finished, this, [this](const QString& text) {
        m_completedText = text;
        m_preview->setResult(text);
        endRun();
        setStatus(tr("Translation finished"));
    });
    connect(&m_translator, &DocumentTranslator::failed, this, [this](const ApiClient::Error& failure) {
        m_preview->setResult(m_completedText);
        endRun();
        setStatus(tr("Error: %1").arg(failure.text()), true);
    });
    connect(&m_translator, &DocumentTranslator::stopped, this, [this]() {
        endRun();
        setStatus(tr("Stopped"));
    });

    WindowState::track(this, WindowState::Id::document);
}

void DocumentDialog::browse()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open document"), QString(),
                                                      tr("Text files (*.txt)"));
    if (path.isEmpty())
        return;
    m_pathEdit->setText(path);
    loadFile();
}

bool DocumentDialog::loadFile()
{
    // Nothing on screen belongs to the path in the box once loading starts, so a
    // run cannot translate the document of a previous path.
    const QString path = m_pathEdit->text().trimmed();
    m_loadedPath.clear();
    m_windows.clear();
    m_completedText.clear();
    m_streaming = false;
    m_preview->setResult(QString());
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_exportButton->setEnabled(false);

    if (path.isEmpty()) {
        setStatus(tr("Choose a .txt file"));
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(tr("Cannot open file: %1").arg(path), true);
        QMessageBox::warning(this, tr("RiipL"), tr("Cannot open file: %1").arg(path));
        return false;
    }

    const QString content = QString::fromUtf8(file.readAll());
    m_loadedPath = path;
    m_windows = DocumentSegmenter::partition(content);
    m_progress->setRange(0, qMax(1, m_windows.size()));
    if (m_windows.isEmpty()) {
        setStatus(tr("No content to translate"));
        return true;
    }
    setStatus(tr("Loaded %1, %2 characters, %3 windows")
                  .arg(QFileInfo(path).fileName())
                  .arg(content.size())
                  .arg(m_windows.size()));
    return true;
}

void DocumentDialog::start()
{
    if (m_running)
        return;
    if (m_pathEdit->text().trimmed() != m_loadedPath && !loadFile())
        return;

    if (m_windows.isEmpty()) {
        QMessageBox::information(this, tr("RiipL"), tr("No content to translate"));
        return;
    }

    m_completedText.clear();
    m_streaming = false;
    m_preview->setResult(QString());
    m_progress->setRange(0, m_windows.size());
    m_progress->setValue(0);
    setRunning(true);
    setStatus(tr("Translating..."));
    m_translator.start(m_windows, m_baseContext);
}

void DocumentDialog::stop()
{
    m_translator.stop();
}

void DocumentDialog::reject()
{
    if (m_running
        && QMessageBox::question(this, tr("RiipL"), tr("Stop the translation and close?"),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes) {
        return;
    }
    if (m_running)
        m_translator.stop();
    QDialog::reject();
}

void DocumentDialog::exportResult()
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
    QFile file(path);
    const QByteArray data = m_completedText.toUtf8();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(data) != data.size()) {
        QMessageBox::warning(this, tr("RiipL"), tr("Cannot write file: %1").arg(path));
        return;
    }
    setStatus(tr("Exported to %1").arg(path));
}

// A run leaves the preview on the document accepted so far, so a window that was
// cut off mid-answer is not exported as if it were translated. The pane keeps
// whatever it shows, so a stop does not wipe the lines already translated.
void DocumentDialog::endRun()
{
    m_streaming = false;
    setRunning(false);
    m_exportButton->setEnabled(!m_completedText.isEmpty());
}

void DocumentDialog::setRunning(bool running)
{
    m_running = running;
    m_translateAction->setEnabled(!running);
    m_stopAction->setEnabled(running);
    m_pathEdit->setEnabled(!running);
    m_browseButton->setEnabled(!running);
    if (running)
        m_exportButton->setEnabled(false);
}

void DocumentDialog::setStatus(const QString& text, bool error)
{
    m_status->setText(text);
    if (error)
        ThemeColors::setTextColor(m_status, ThemeColors::errorText(m_status));
    else
        m_status->setPalette(QPalette());
}
