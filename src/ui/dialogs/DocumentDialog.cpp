#include "DocumentDialog.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int kChunkLimit = 2000;
}

DocumentDialog::DocumentDialog(const TranslationContext& baseContext, QWidget* parent)
    : QDialog(parent)
    , m_baseContext(baseContext)
{
    setWindowTitle(tr("Document translation"));
    resize(720, 560);

    auto* layout = new QVBoxLayout(this);

    auto* fileRow = new QHBoxLayout();
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText(tr("Choose a .txt / .md / .json / .html file"));
    auto* browseButton = new QPushButton(tr("Browse..."), this);
    fileRow->addWidget(m_pathEdit, 1);
    fileRow->addWidget(browseButton);
    layout->addLayout(fileRow);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    layout->addWidget(m_progress);

    m_status = new QLabel(tr("Ready"), this);
    layout->addWidget(m_status);

    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);
    layout->addWidget(m_preview, 1);

    auto* buttonRow = new QHBoxLayout();
    m_startButton = new QPushButton(tr("Start translation"), this);
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_exportButton = new QPushButton(tr("Export translation..."), this);
    m_exportButton->setEnabled(false);
    auto* closeButton = new QPushButton(tr("Close"), this);
    buttonRow->addWidget(m_startButton);
    buttonRow->addWidget(m_cancelButton);
    buttonRow->addWidget(m_exportButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    connect(browseButton, &QPushButton::clicked, this, &DocumentDialog::browse);
    connect(m_startButton, &QPushButton::clicked, this, &DocumentDialog::start);
    connect(m_cancelButton, &QPushButton::clicked, this, &DocumentDialog::cancel);
    connect(m_exportButton, &QPushButton::clicked, this, &DocumentDialog::exportResult);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    connect(&m_engine, &TranslationEngine::finished, this, [this](const QString& result) {
        m_translatedChunks.append(result);
        m_currentChunk++;
        m_progress->setValue(m_currentChunk);
        m_status->setText(tr("Translated %1/%2 chunks").arg(m_currentChunk).arg(m_chunks.size()));
        m_preview->setPlainText(m_translatedChunks.join(QStringLiteral("\n\n")));
        if (m_currentChunk < m_chunks.size()) {
            translateNextChunk();
        } else {
            m_running = false;
            m_startButton->setEnabled(true);
            m_exportButton->setEnabled(!m_translatedChunks.isEmpty());
            m_status->setText(tr("Translation finished"));
        }
    });
    connect(&m_engine, &TranslationEngine::error, this, [this](const QString& message) {
        m_running = false;
        m_startButton->setEnabled(true);
        m_status->setText(tr("Error: %1").arg(message));
    });
}

void DocumentDialog::browse()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open document"), QString(),
                                                      tr("Documents (*.txt *.md *.json *.html *.htm *.xml *.csv);;All files (*)"));
    if (path.isEmpty())
        return;
    m_pathEdit->setText(path);
    loadFile();
}

void DocumentDialog::loadFile()
{
    const QString path = m_pathEdit->text().trimmed();
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("RiipL"), tr("Cannot open file: %1").arg(path));
        return;
    }
    m_sourceContent = QString::fromUtf8(file.readAll());
    m_chunks = buildChunks(m_sourceContent);
    m_translatedChunks.clear();
    m_currentChunk = 0;
    m_preview->clear();
    m_progress->setMaximum(qMax(1, m_chunks.size()));
    m_progress->setValue(0);
    m_exportButton->setEnabled(false);
    m_status->setText(tr("Loaded %1, %2 characters, %3 chunks").arg(QFileInfo(path).fileName())
                          .arg(m_sourceContent.size()).arg(m_chunks.size()));
}

QStringList DocumentDialog::buildChunks(const QString& content) const
{
    QStringList chunks;
    const QStringList paragraphs = content.split(QStringLiteral("\n\n"));
    QString current;
    for (const QString& paragraph : paragraphs) {
        if (paragraph.size() > kChunkLimit) {
            if (!current.isEmpty()) {
                chunks << current;
                current.clear();
            }
            for (int offset = 0; offset < paragraph.size(); offset += kChunkLimit)
                chunks << paragraph.mid(offset, kChunkLimit);
            continue;
        }
        if (!current.isEmpty() && current.size() + paragraph.size() + 2 > kChunkLimit) {
            chunks << current;
            current.clear();
        }
        if (!current.isEmpty())
            current += QStringLiteral("\n\n");
        current += paragraph;
    }
    if (!current.isEmpty())
        chunks << current;
    return chunks;
}

void DocumentDialog::start()
{
    if (m_running)
        return;
    if (m_pathEdit->text().trimmed().isEmpty() || m_sourceContent.isEmpty())
        loadFile();
    if (m_chunks.isEmpty()) {
        QMessageBox::information(this, tr("RiipL"), tr("No content to translate"));
        return;
    }
    m_translatedChunks.clear();
    m_currentChunk = 0;
    m_preview->clear();
    m_running = true;
    m_startButton->setEnabled(false);
    m_exportButton->setEnabled(false);
    translateNextChunk();
}

void DocumentDialog::translateNextChunk()
{
    TranslationContext context = m_baseContext;
    context.sourceText = m_chunks.at(m_currentChunk);
    m_engine.translateText(context);
}

void DocumentDialog::cancel()
{
    if (!m_running)
        return;
    m_engine.stop();
    m_running = false;
    m_startButton->setEnabled(true);
    m_status->setText(tr("Cancelled"));
}

void DocumentDialog::exportResult()
{
    if (m_translatedChunks.isEmpty())
        return;
    const QString sourcePath = m_pathEdit->text().trimmed();
    const QFileInfo info(sourcePath);
    QString suggested = info.absolutePath() + QStringLiteral("/%1.translated.%2")
                            .arg(info.completeBaseName(), info.suffix().isEmpty() ? QStringLiteral("txt") : info.suffix());
    const QString path = QFileDialog::getSaveFileName(this, tr("Export translation"), suggested);
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("RiipL"), tr("Cannot write file: %1").arg(path));
        return;
    }
    file.write(m_translatedChunks.join(QStringLiteral("\n\n")).toUtf8());
    m_status->setText(tr("Exported to %1").arg(path));
}
