#include "DocumentTranslator.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <utility>

namespace {

// Pause before each attempt after the first.
constexpr int kRetryDelayMs = 1000;
// Characters of the request digest that name a shard's cache file.
constexpr int kShardDigestCharacters = 8;

// Accepted lines as the JSON object a cache file holds.
QString answerJson(const QStringList& lines)
{
    QJsonObject answer;
    for (int line = 0; line < lines.size(); ++line)
        answer.insert(QString::number(line + 1), lines.at(line));
    return QString::fromUtf8(QJsonDocument(answer).toJson(QJsonDocument::Indented));
}

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
                [this, index](const ApiClient::Error&) { retryOrFail(index); });
        connect(worker.retryTimer, &QTimer::timeout, this,
                [this, index]() { resendWindow(index); });
        m_workers.append(worker);
    }
}

void DocumentTranslator::start(const QVector<DocumentWindow>& windows,
                               const TranslationContext& context, DocumentCache cache)
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
    m_cache = std::move(cache);
    m_shardIds.clear();
    m_failedShards.clear();
    m_nextWindow = 0;
    m_completed = 0;

    // The windows the cache answers are accepted up front, the rest reaches the
    // model; the failures of the previous run go with it.
    if (m_cache.isValid()) {
        m_cache.removeFailures();
        m_shardIds.reserve(m_windows.size());
        for (int index = 0; index < m_windows.size(); ++index) {
            m_shardIds.append(shardId(index));
            const std::optional<QStringList> lines = cachedTranslation(index);
            if (!lines)
                continue;
            m_translations[index] = *lines;
            ++m_completed;
        }
    }

    if (m_windows.isEmpty()) {
        emit finished(QString());
        return;
    }

    // A document the cache holds in full is answered without a run.
    if (m_completed == m_windows.size()) {
        emit progressChanged(m_completed, m_windows.size());
        emit finished(DocumentSegmenter::assemble(m_windows, m_translations));
        return;
    }

    ensureWorkers(qMin(requestedWorkers(), m_windows.size()));
    m_active = true;
    // Reported once the run is under way, so a slot that stops here stops it for
    // good.
    emit progressChanged(m_completed, m_windows.size());
    // The windows the cache answered are part of the document already.
    if (m_completed > 0)
        emit windowTranslated(DocumentSegmenter::assemble(m_windows, m_translations));
    dispatchPending();
}

// The request of one window: its collapsed lines, its neighbours as context.
DocumentWindowPrompt DocumentTranslator::windowPrompt(int index) const
{
    DocumentWindowPrompt prompt;
    for (const DocumentLine& line : m_windows.at(index).lines)
        prompt.lines.append(line.text);
    if (index > 0)
        prompt.previous = m_sources.at(index - 1);
    if (index + 1 < m_windows.size())
        prompt.next = m_sources.at(index + 1);
    return prompt;
}

// The request decides the answer, so the digest covers the body the window is sent
// with, extra body included; the window number keeps windows of a document apart.
QString DocumentTranslator::shardId(int index) const
{
    if (index < m_shardIds.size())
        return m_shardIds.at(index);

    TranslationContext context = m_context;
    context.sourceText = m_sources.at(index);
    const PromptBuilder::Result prompt =
        PromptBuilder::buildDocument(context, windowPrompt(index));

    ConfigManager* config = ConfigManager::instance();
    const QJsonObject body =
        TranslationEngine::buildRequestBody(prompt, config->boolValue(Keys::apiStream));

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QJsonDocument(body).toJson(QJsonDocument::Compact));
    hash.addData(config->stringValue(Keys::apiExtraBody).toUtf8());
    return QString::number(index) + QLatin1Char('.')
        + QString::fromLatin1(hash.result().toHex().left(kShardDigestCharacters));
}

std::optional<QStringList> DocumentTranslator::cachedTranslation(int index) const
{
    const std::optional<QString> answer = m_cache.cachedShard(m_shardIds.value(index));
    if (!answer)
        return std::nullopt;
    return DocumentSegmenter::splitTranslation(m_windows.at(index), *answer);
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
        if (worker.engine->isBusy())
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
        // The windows the cache answered are done already and are passed over.
        while (m_nextWindow < m_windows.size() && !m_translations.at(m_nextWindow).isEmpty())
            ++m_nextWindow;
        if (m_nextWindow >= m_windows.size())
            return;
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

    TranslationContext context = m_context;
    context.sourceText = m_sources.at(index);
    worker.engine->translateDocument(context, windowPrompt(index));
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
        // Kept as it came back, so a window that keeps failing can be inspected.
        m_cache.storeFailure(m_shardIds.value(index), response);
        retryOrFail(workerIndex);
        return;
    }

    m_translations[index] = *lines;
    // The answer is kept for the next run; the failures of the attempts before it
    // go.
    m_cache.storeShard(m_shardIds.value(index), answerJson(*lines));
    m_cache.removeFailure(m_shardIds.value(index));
    worker.window = -1;
    ++m_completed;
    emit progressChanged(m_completed, m_windows.size());

    // The document is reported whole on every accepted window, so an answer lands
    // at its own place whatever order the answers arrive in; a window the run has
    // not accepted yet keeps its source text.
    const QString document = DocumentSegmenter::assemble(m_windows, m_translations);
    emit windowTranslated(document);

    if (allWindowsSettled()) {
        finishRun();
        return;
    }
    dispatchPending();
}

// A window that has used its attempts is left out while the run carries on, so one
// bad reply does not cost the rest of the document.
void DocumentTranslator::retryOrFail(int workerIndex)
{
    if (!m_active)
        return;
    Worker& worker = m_workers[workerIndex];
    const int retries = qMax(0, ConfigManager::instance()->intValue(Keys::documentRetryCount));
    if (worker.attempts <= retries) {
        // The retry is settled before the pause starts, so a slot that stops the
        // run here stops it for good.
        worker.retryTimer->start(kRetryDelayMs * worker.attempts);
        return;
    }

    const int window = worker.window;
    worker.window = -1;
    if (window >= 0)
        m_failedShards.append(shardId(window));

    if (allWindowsSettled()) {
        finishRun();
        return;
    }
    dispatchPending();
}

bool DocumentTranslator::allWindowsSettled() const
{
    return m_completed + m_failedShards.size() == m_windows.size();
}

void DocumentTranslator::finishRun()
{
    m_active = false;
    if (!m_failedShards.isEmpty()) {
        emit failed(m_failedShards);
        return;
    }
    emit finished(DocumentSegmenter::assemble(m_windows, m_translations));
}
