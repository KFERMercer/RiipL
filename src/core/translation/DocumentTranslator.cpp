#include "DocumentTranslator.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

#include <QString>

#include <optional>
#include <utility>

namespace {

// Pause before each attempt after the first.
constexpr int kRetryDelayMs = 1000;

} // namespace

DocumentTranslator::DocumentTranslator(QObject* parent)
    : QObject(parent)
{
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, &DocumentTranslator::translateWindow);

    connect(&m_engine, &TranslationEngine::partialDelta,
            this, &DocumentTranslator::windowStreamed);
    connect(&m_engine, &TranslationEngine::finished,
            this, &DocumentTranslator::handleWindowFinished);
    connect(&m_engine, &TranslationEngine::error, this, &DocumentTranslator::retryOrFail);
}

void DocumentTranslator::start(const QVector<DocumentWindow>& windows,
                               const TranslationContext& context)
{
    cancelRun();
    m_windows = windows;
    m_sources.clear();
    m_sources.reserve(m_windows.size());
    for (const DocumentWindow& window : std::as_const(m_windows))
        m_sources.append(window.source());
    m_translations.clear();
    m_rendered.clear();
    m_context = context;
    m_attempts = 0;

    if (m_windows.isEmpty()) {
        emit finished(QString());
        return;
    }
    m_active = true;
    emit progressChanged(0, m_windows.size());
    translateWindow();
}

void DocumentTranslator::stop()
{
    if (!m_active)
        return;
    cancelRun();
    emit stopped();
}

void DocumentTranslator::cancelRun()
{
    m_active = false;
    m_retryTimer.stop();
    if (m_engine.busy())
        m_engine.stop();
}

void DocumentTranslator::translateWindow()
{
    if (!m_active)
        return;
    const int index = m_translations.size();
    if (index >= m_windows.size())
        return;
    ++m_attempts;

    DocumentWindowPrompt prompt;
    // A line repeated in the document reaches the model once; the text is put
    // back at every occurrence when the window is rendered.
    for (const DocumentLine& line : m_windows.at(index).lines)
        prompt.lines.append(line.text);
    // Neighbouring windows supply the surrounding prose; the document edges have
    // none.
    if (index > 0)
        prompt.previous = m_sources.at(index - 1);
    if (index + 1 < m_windows.size())
        prompt.next = m_sources.at(index + 1);

    TranslationContext context = m_context;
    context.sourceText = m_sources.at(index);
    m_engine.translateDocument(context, prompt);
}

void DocumentTranslator::handleWindowFinished(const QString& response)
{
    if (!m_active)
        return;
    const int index = m_translations.size();
    if (index >= m_windows.size())
        return;

    const DocumentWindow& window = m_windows.at(index);
    const std::optional<QStringList> lines = DocumentSegmenter::splitTranslation(window, response);
    if (!lines) {
        ApiClient::Error failure;
        failure.code = ApiClient::ErrorCode::LineCountMismatch;
        failure.detail = QString::number(window.lineCount());
        retryOrFail(failure);
        return;
    }

    m_attempts = 0;
    m_translations.append(*lines);
    m_rendered.append(DocumentSegmenter::renderWindow(window, *lines));
    const QString text = m_rendered.join(QLatin1Char('\n'));
    emit windowTranslated(text);
    emit progressChanged(m_translations.size(), m_windows.size());

    if (m_translations.size() < m_windows.size()) {
        translateWindow();
        return;
    }
    m_active = false;
    // The finished document is the segmenter's rebuild of every window.
    emit finished(DocumentSegmenter::assemble(m_windows, m_translations));
}

void DocumentTranslator::retryOrFail(const ApiClient::Error& failure)
{
    if (!m_active)
        return;
    const int retries = qMax(0, ConfigManager::instance()->intValue(Keys::documentRetryCount));
    if (m_attempts > retries) {
        m_active = false;
        emit failed(failure);
        return;
    }
    // The retry is settled before anyone is told, so a slot that stops the run
    // here stops it for good.
    m_retryTimer.start(kRetryDelayMs * m_attempts);
    emit windowRestarted();
}
