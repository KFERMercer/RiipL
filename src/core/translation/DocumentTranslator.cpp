#include "DocumentTranslator.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/translation/TranslationEngine.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMultiMap>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {

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

DocumentTranslator::~DocumentTranslator()
{
    cancelRun();
}

void DocumentTranslator::start(const QVector<DocumentWindow>& windows,
                               const TranslationContext& context, std::optional<DocumentCache> cache,
                               const DocumentRunOptions& options)
{
    cancelRun();
    m_windows = windows;
    m_sources.clear();
    m_sources.reserve(m_windows.size());
    for (const DocumentWindow& window : std::as_const(m_windows))
        m_sources.append(window.source());

    m_translations.clear();
    // A window without an answer yet renders as its own source text.
    m_translations.resize(m_windows.size());
    m_jobs.assign(m_windows.size(), 0);
    m_samples.assign(m_windows.size(), QString());
    m_attempts.assign(m_windows.size(), 0);
    m_settled.assign(m_windows.size(), false);
    m_context = context;
    m_options = options;
    m_retries = options.retryCount
        ? (std::max)(0, *options.retryCount)
        : (std::max)(0, ConfigManager::instance()->intValue(Keys::documentRetryCount));
    m_retryDelayMs = (std::max)(0, options.retryDelayMs);
    m_cache = std::move(cache);
    m_failedSamples.clear();
    m_documentPending = false;
    m_nextWindow = 0;
    m_completed = 0;
    m_serial = !ConfigManager::instance()->boolValue(Keys::documentConcurrent);

    if (m_windows.isEmpty()) {
        emit finished(QString());
        return;
    }

    m_active = true;
    // Reported with the run under way, so a slot that stops here stops it.
    emit progressChanged(m_completed, m_windows.size());
    // Marked changed so the start reports the document as it stands.
    m_documentPending = true;
    reportDocument();
    dispatchPending();
}

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
    // Any retry a window is waiting out belongs to the run that just ended.
    ++m_generation;
    m_active = false;
    cancelJobs();
    m_nextWindow = 0;
}

void DocumentTranslator::cancelJobs()
{
    if (m_jobs.isEmpty())
        return;
    TranslationPool* pool = TranslationPool::instance();
    for (TranslationPool::JobId& job : m_jobs) {
        pool->cancel(job);
        job = 0;
    }
}

void DocumentTranslator::dispatchPending()
{
    if (!m_active)
        return;

    while (m_nextWindow < m_windows.size()
           && (m_settled.at(m_nextWindow)
               || (!m_serial && m_attempts.at(m_nextWindow) > 0)))
        ++m_nextWindow;
    if (m_nextWindow >= m_windows.size() || (m_serial && m_attempts.at(m_nextWindow) > 0)) {
        reportDocument();
        return;
    }

    // The walk takes one window per turn of the event loop, so the request of a
    // window leaves as soon as the window is built.
    const quint64 generation = m_generation;
    const int index = m_nextWindow;
    processWindow(index);
    // A slot reached from the window may have replaced this run.
    if (generation != m_generation)
        return;
    if (m_serial && !m_settled.at(index))
        return;
    ++m_nextWindow;
    QMetaObject::invokeMethod(this, &DocumentTranslator::dispatchPending, Qt::QueuedConnection);
}

void DocumentTranslator::processWindow(int index)
{
    const quint64 generation = m_generation;
    const QString sample = sampleDigest(index);
    if (m_cache) {
        if (const std::optional<QStringList> lines = cachedTranslation(index, sample)) {
            // The answer stands in place of the attempts that failed.
            m_cache->removeFailure(sample);
            acceptWindow(index, *lines, false);
            return;
        }
    }

    // The windows the cache answered are reported before the run waits on the model.
    reportDocument();
    if (generation != m_generation)
        return;
    sendWindow(index, sample);
}

// The digest goes with the attempt, so the answer is kept under the request that
// produced it.
void DocumentTranslator::sendWindow(int index, const QString& sample)
{
    const int priority = m_attempts.at(index) > 0 ? m_options.priorityRetry
                                                  : m_options.priority;
    ++m_attempts[index];
    m_samples[index] = sample;
    if (m_cache)
        m_cache->removeFailure(sample);

    TranslationContext context = m_context;
    context.sourceText = m_sources.at(index);
    const PromptBuilder::Result prompt =
        PromptBuilder::buildDocument(context, windowPrompt(index));
    const QJsonObject body = TranslationEngine::buildRequestBody(
        prompt, ConfigManager::instance()->boolValue(Keys::apiStream));

    m_jobs[index] = TranslationPool::instance()->enqueue(body, priority,
        [this, index](const QString& response) { handleWindowFinished(index, response); },
        {},
        [this, index](const ApiClient::Error&) { handleWindowError(index); });
}

// An answer the run waited for is reported as it lands; the windows the cache
// answered are reported together.
void DocumentTranslator::acceptWindow(int index, const QStringList& lines, bool report)
{
    const quint64 generation = m_generation;
    m_translations[index] = lines;
    m_settled[index] = true;
    ++m_completed;
    m_documentPending = true;
    emit progressChanged(m_completed, m_windows.size());
    if (report)
        reportDocument();
    // A slot reached from the report may have replaced this run, whose end this is
    // not.
    if (generation == m_generation && allWindowsSettled())
        finishRun();
}

// A window the cache answers costs no wait, so the document is not rebuilt for each
// of them.
void DocumentTranslator::reportDocument()
{
    if (!m_documentPending)
        return;
    m_documentPending = false;
    emit windowTranslated(DocumentSegmenter::assemble(m_windows, m_translations));
}

void DocumentTranslator::handleWindowFinished(int index, const QString& response)
{
    if (!m_active || m_settled.at(index))
        return;
    m_jobs[index] = 0;

    const DocumentWindow& window = m_windows.at(index);
    const std::optional<QStringList> lines = DocumentSegmenter::splitTranslation(window, response);
    if (!lines) {
        // Kept as it came back, so a window that keeps failing can be inspected.
        if (m_cache)
            m_cache->storeFailure(m_samples.at(index), response);
        retryOrFail(index);
        return;
    }

    // The answer is kept for the next run; the failures of the attempts before it
    // go.
    if (m_cache) {
        m_cache->storeAnswer(m_samples.at(index), PromptBuilder::documentWindowData(*lines));
        m_cache->removeFailure(m_samples.at(index));
    }
    acceptWindow(index, *lines, true);
    if (m_active)
        dispatchPending();
}

void DocumentTranslator::handleWindowError(int index)
{
    if (!m_active || m_settled.at(index))
        return;
    m_jobs[index] = 0;
    retryOrFail(index);
}

// A window that has used its attempts is left out while the run carries on, so one
// bad reply does not cost the rest of the document.
void DocumentTranslator::retryOrFail(int index)
{
    if (!m_active)
        return;

    const int retries = m_retries;
    if (m_attempts.at(index) <= retries) {
        const quint64 generation = m_generation;
        QTimer::singleShot(m_retryDelayMs * m_attempts.at(index), this, [this, index, generation]() {
            if (!m_active || generation != m_generation || m_settled.at(index)
                || m_jobs.at(index) != 0)
                return;
            sendWindow(index, sampleDigest(index));
        });
        return;
    }

    m_settled[index] = true;
    m_failedSamples.append(m_samples.at(index).left(kReportedSampleCharacters));

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
        // The document the run built goes out before the windows it left out.
        reportDocument();
        emit failed(m_failedSamples);
        return;
    }
    emit finished(DocumentSegmenter::assemble(m_windows, m_translations));
}
