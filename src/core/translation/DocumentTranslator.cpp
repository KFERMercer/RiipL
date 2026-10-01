#include "DocumentTranslator.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

#include <QTimer>

#include <utility>

namespace {

// Pause before each attempt after the first.
constexpr int kRetryDelayMs = 1000;

}

DocumentTranslator::DocumentTranslator(QObject* parent)
    : QObject(parent)
{
}

int DocumentTranslator::requestedWorkers() const
{
    ConfigManager* config = ConfigManager::instance();
    // The switch turns concurrent translation off; the limit is what a run may
    // have in flight when it is on.
    if (!config->boolValue(Keys::documentConcurrent))
        return 1;
    return qMax(1, config->intValue(Keys::apiMaxConcurrency));
}

// The pool grows to the largest count a run has needed and is kept, so a second
// run reuses the requests instead of building new ones.
void DocumentTranslator::ensureWorkers(int count)
{
    while (m_workers.size() < count) {
        const int index = m_workers.size();
        Worker worker;
        worker.engine = new TranslationEngine(this);
        worker.retryTimer = new QTimer(this);
        worker.retryTimer->setSingleShot(true);

        connect(worker.engine, &TranslationEngine::finished, this,
                [this, index](const QString& response) { handleWindowFinished(index, response); });
        connect(worker.engine, &TranslationEngine::error, this,
                [this, index](const ApiClient::Error& failure) { retryOrFail(index, failure); });
        connect(worker.retryTimer, &QTimer::timeout, this,
                [this, index]() { resendWindow(index); });
        m_workers.append(worker);
    }
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
    // A window without an answer yet renders as its own source text, which is
    // what the document shows for the windows a run has not reached.
    m_translations.resize(m_windows.size());
    m_context = context;
    m_nextWindow = 0;
    m_completed = 0;

    if (m_windows.isEmpty()) {
        emit finished(QString());
        return;
    }

    ensureWorkers(qMin(requestedWorkers(), m_windows.size()));
    m_active = true;
    emit progressChanged(0, m_windows.size());
    dispatchPending();
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
    for (Worker& worker : m_workers) {
        worker.retryTimer->stop();
        if (worker.engine->busy())
            worker.engine->stop();
        worker.window = -1;
    }
    m_nextWindow = 0;
}

void DocumentTranslator::dispatchPending()
{
    if (!m_active)
        return;
    for (int index = 0; index < m_workers.size() && m_nextWindow < m_windows.size(); ++index) {
        Worker& worker = m_workers[index];
        if (worker.window >= 0)
            continue;
        sendWindow(index, m_nextWindow);
        ++m_nextWindow;
    }
}

void DocumentTranslator::sendWindow(int workerIndex, int window)
{
    Worker& worker = m_workers[workerIndex];
    worker.window = window;
    worker.attempts = 0;
    resendWindow(workerIndex);
}

void DocumentTranslator::resendWindow(int workerIndex)
{
    if (!m_active)
        return;
    Worker& worker = m_workers[workerIndex];
    const int index = worker.window;
    if (index < 0)
        return;
    ++worker.attempts;

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
    worker.engine->translateDocument(context, prompt);
}

void DocumentTranslator::handleWindowFinished(int workerIndex, const QString& response)
{
    if (!m_active)
        return;
    Worker& worker = m_workers[workerIndex];
    const int index = worker.window;
    if (index < 0)
        return;

    const DocumentWindow& window = m_windows.at(index);
    const std::optional<QStringList> lines = DocumentSegmenter::splitTranslation(window, response);
    if (!lines) {
        ApiClient::Error failure;
        failure.code = ApiClient::ErrorCode::LineCountMismatch;
        failure.detail = QString::number(window.lineCount());
        retryOrFail(workerIndex, failure);
        return;
    }

    m_translations[index] = *lines;
    worker.window = -1;
    ++m_completed;
    emit progressChanged(m_completed, m_windows.size());

    // The document is reported whole on every accepted window, so an answer lands
    // at its own place whatever order the answers arrive in; a window the run has
    // not accepted yet keeps its source text.
    const QString document = DocumentSegmenter::assemble(m_windows, m_translations);
    emit windowTranslated(document);

    if (m_completed == m_windows.size()) {
        m_active = false;
        emit finished(document);
        return;
    }
    dispatchPending();
}

void DocumentTranslator::retryOrFail(int workerIndex, const ApiClient::Error& failure)
{
    if (!m_active)
        return;
    Worker& worker = m_workers[workerIndex];
    const int retries = qMax(0, ConfigManager::instance()->intValue(Keys::documentRetryCount));
    if (worker.attempts > retries) {
        // The run gives up here, so the requests beside the failed window are
        // dropped rather than left on the wire.
        cancelRun();
        emit failed(failure);
        return;
    }
    // The retry is settled before the pause starts, so a slot that stops the run
    // here stops it for good.
    worker.retryTimer->start(kRetryDelayMs * worker.attempts);
}
