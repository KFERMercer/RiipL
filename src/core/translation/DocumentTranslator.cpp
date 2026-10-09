#include "DocumentTranslator.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMultiMap>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {

// Pause before each attempt after the first.
constexpr int kRetryDelayMs = 1000;
// Characters of the request digest that name a window the run left out.
constexpr int kReportedSampleCharacters = 8;

QJsonArray canonicalHeaders(const QString& raw)
{
    const QMultiMap<QByteArray, QByteArray> headers =
        ApiClient::parseCustomHeaders(raw).toMultiMap();
    QJsonArray pairs;
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        pairs.append(QJsonArray{QString::fromLatin1(it.key()), QString::fromLatin1(it.value())});
    return pairs;
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
    return (std::max)(1, config->intValue(Keys::apiMaxConcurrency));
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
        connect(worker.engine, &TranslationEngine::errorOccurred, this,
                [this, index](const ApiClient::Error&) { handleWindowError(index); });
        connect(worker.retryTimer, &QTimer::timeout, this,
                [this, index]() { resendWindow(index); });
        m_workers.append(worker);
    }
}

void DocumentTranslator::start(const QVector<DocumentWindow>& windows,
                               const TranslationContext& context, std::optional<DocumentCache> cache)
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
    m_failedSamples.clear();
    m_nextWindow = 0;
    m_completed = 0;

    // The windows the cache answers are accepted up front, the rest reaches the
    // model.
    if (m_cache) {
        for (int index = 0; index < m_windows.size(); ++index) {
            const QString sample = sampleDigest(index);
            const std::optional<QStringList> lines = cachedTranslation(index, sample);
            if (!lines)
                continue;
            // The answer stands in place of the attempts that failed.
            m_cache->removeFailure(sample);
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

    const int workers = (std::min)(requestedWorkers(), static_cast<int>(m_windows.size()));
    ensureWorkers(workers);
    m_inFlightLimit = workers;
    m_inFlight = 0;
    m_waiting.clear();
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

// The answer follows what the model is asked — the endpoint, the headers and the
// body — and nothing that only carries the request there. The window number is no
// part of it, so any document sending the same request reuses the same answer.
QString DocumentTranslator::sampleDigest(int index) const
{
    TranslationContext context = m_context;
    context.sourceText = m_sources.at(index);
    const PromptBuilder::Result prompt =
        PromptBuilder::buildDocument(context, windowPrompt(index));

    QJsonObject body = ApiClient::withExtraBody(TranslationEngine::buildRequestBody(
        prompt, ConfigManager::instance()->boolValue(Keys::apiStream)));
    // Streaming decides how the answer arrives, not what it says.
    body.remove(QStringLiteral("stream"));

    const ApiClient::Connection connection = ApiClient::Connection::configured();
    const QJsonObject sample{
        {QStringLiteral("base_url"), connection.baseUrl},
        {QStringLiteral("headers"), canonicalHeaders(connection.customHeaders)},
        {QStringLiteral("body"), body}
    };

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QJsonDocument(sample).toJson(QJsonDocument::Compact));
    return QString::fromLatin1(hash.result().toHex());
}

std::optional<QStringList> DocumentTranslator::cachedTranslation(int index,
                                                                 const QString& sample) const
{
    if (!m_cache)
        return std::nullopt;
    const std::optional<QString> answer = m_cache->cachedAnswer(sample);
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
        worker.sample.clear();
    }
    m_nextWindow = 0;
    m_inFlight = 0;
    m_waiting.clear();
}

void DocumentTranslator::dispatchPending()
{
    if (!m_active || m_dispatching)
        return;
    m_dispatching = true;
    // A window that waited for a free slot comes first: it has spent an attempt already.
    while (m_active && m_inFlight < m_inFlightLimit && !m_waiting.isEmpty())
        resendWindow(m_waiting.takeFirst());
    // A send can fail on this stack and free its worker again, so windows are
    // handed out until no worker can take one.
    for (bool dispatched = true; dispatched && m_active && m_inFlight < m_inFlightLimit;) {
        dispatched = false;
        for (int index = 0; index < m_workers.size() && m_inFlight < m_inFlightLimit; ++index) {
            Worker& worker = m_workers[index];
            if (worker.window >= 0)
                continue;
            // The windows the cache answered are done already and are passed over.
            while (m_nextWindow < m_windows.size() && !m_translations.at(m_nextWindow).isEmpty())
                ++m_nextWindow;
            if (m_nextWindow >= m_windows.size())
                break;
            // Taken before the send, so a send that reports back on this stack
            // does not see this window again.
            const int window = m_nextWindow++;
            sendWindow(index, window);
            dispatched = true;
        }
    }
    m_dispatching = false;
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
    // The endpoint is never given more requests than it has taken: a send that is
    // due waits for a free slot.
    if (m_inFlight >= m_inFlightLimit) {
        if (!m_waiting.contains(workerIndex))
            m_waiting.append(workerIndex);
        return;
    }
    ++worker.attempts;
    ++m_inFlight;

    // Taken here, where the request goes out, so the answer is kept under the request
    // that produced it; the record of an earlier attempt no longer describes it.
    worker.sample = sampleDigest(index);
    if (m_cache)
        m_cache->removeFailure(worker.sample);

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
    --m_inFlight;

    const DocumentWindow& window = m_windows.at(index);
    const std::optional<QStringList> lines = DocumentSegmenter::splitTranslation(window, response);
    if (!lines) {
        // Kept as it came back, so a window that keeps failing can be inspected.
        if (m_cache)
            m_cache->storeFailure(worker.sample, response);
        retryOrFail(workerIndex);
        return;
    }

    m_translations[index] = *lines;
    // The answer is kept for the next run; the failures of the attempts before it
    // go.
    if (m_cache) {
        m_cache->storeAnswer(worker.sample, PromptBuilder::documentWindowData(*lines));
        m_cache->removeFailure(worker.sample);
    }
    worker.window = -1;
    worker.sample.clear();
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

// A failed request is the endpoint saying it cannot take as many: the run keeps
// fewer in flight, so the retry does not compete for the same context again.
void DocumentTranslator::handleWindowError(int workerIndex)
{
    if (!m_active)
        return;
    if (m_workers.at(workerIndex).window < 0)
        return;
    --m_inFlight;
    m_inFlightLimit = (std::max)(1, m_inFlightLimit / 2);
    retryOrFail(workerIndex);
}

// A window that has used its attempts is left out while the run carries on, so one
// bad reply does not cost the rest of the document.
void DocumentTranslator::retryOrFail(int workerIndex)
{
    if (!m_active)
        return;
    Worker& worker = m_workers[workerIndex];
    const int retries = (std::max)(0, ConfigManager::instance()->intValue(Keys::documentRetryCount));
    if (worker.attempts <= retries) {
        // The retry is settled before the pause starts, so a slot that stops the
        // run here stops it for good.
        worker.retryTimer->start(kRetryDelayMs * worker.attempts);
        return;
    }

    const int window = worker.window;
    worker.window = -1;
    if (window >= 0)
        m_failedSamples.append(worker.sample.left(kReportedSampleCharacters));
    worker.sample.clear();

    if (allWindowsSettled()) {
        finishRun();
        return;
    }
    dispatchPending();
}

bool DocumentTranslator::allWindowsSettled() const
{
    return m_completed + m_failedSamples.size() == m_windows.size();
}

void DocumentTranslator::finishRun()
{
    m_active = false;
    if (!m_failedSamples.isEmpty()) {
        emit failed(m_failedSamples);
        return;
    }
    emit finished(DocumentSegmenter::assemble(m_windows, m_translations));
}
