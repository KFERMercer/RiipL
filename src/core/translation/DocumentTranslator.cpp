#include "DocumentTranslator.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/translation/TranslationEngine.h"

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

DocumentTranslator::~DocumentTranslator()
{
    cancelRun();
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
    // A window without an answer yet renders as its own source text.
    m_translations.resize(m_windows.size());
    m_jobs.assign(m_windows.size(), 0);
    m_samples.assign(m_windows.size(), QString());
    m_attempts.assign(m_windows.size(), 0);
    m_settled.assign(m_windows.size(), false);
    m_context = context;
    m_cache = std::move(cache);
    m_failedSamples.clear();
    m_nextWindow = 0;
    m_completed = 0;
    m_serial = !ConfigManager::instance()->boolValue(Keys::documentConcurrent);

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
            m_settled[index] = true;
            ++m_completed;
        }
    }

    if (m_windows.isEmpty()) {
        emit finished(QString());
        return;
    }

    // A document the cache holds in full is answered without a run.
    if (allWindowsSettled()) {
        emit progressChanged(m_completed, m_windows.size());
        emit finished(DocumentSegmenter::assemble(m_windows, m_translations));
        return;
    }

    m_active = true;
    // Reported once the run is under way, so a slot that stops here stops it for
    // good.
    emit progressChanged(m_completed, m_windows.size());
    // The windows the cache answered are part of the document already.
    if (m_completed > 0)
        emit windowTranslated(DocumentSegmenter::assemble(m_windows, m_translations));
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

    if (m_serial) {
        // The window in hand has to settle, retries included, before the next one
        // starts.
        while (m_nextWindow < m_windows.size() && m_settled.at(m_nextWindow))
            ++m_nextWindow;
        if (m_nextWindow >= m_windows.size() || m_attempts.at(m_nextWindow) > 0)
            return;
        submitWindow(m_nextWindow);
        return;
    }

    for (int index = 0; index < m_windows.size(); ++index) {
        if (!m_settled.at(index) && m_attempts.at(index) == 0)
            submitWindow(index);
    }
}

void DocumentTranslator::submitWindow(int index)
{
    const int priority = m_attempts.at(index) > 0 ? DocumentPriority::windowRetry
                                                  : DocumentPriority::window;
    ++m_attempts[index];

    // Taken with the attempt, so the answer is kept under the request that
    // produced it; the record of an earlier attempt no longer describes it.
    m_samples[index] = sampleDigest(index);
    if (m_cache)
        m_cache->removeFailure(m_samples.at(index));

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

    m_translations[index] = *lines;
    // The answer is kept for the next run; the failures of the attempts before it
    // go.
    if (m_cache) {
        m_cache->storeAnswer(m_samples.at(index), PromptBuilder::documentWindowData(*lines));
        m_cache->removeFailure(m_samples.at(index));
    }
    m_settled[index] = true;
    ++m_completed;
    emit progressChanged(m_completed, m_windows.size());

    // The document is reported whole on every accepted window, so an answer lands
    // at its own place whatever order the answers arrive in; a window the run has
    // not accepted yet keeps its source text.
    emit windowTranslated(DocumentSegmenter::assemble(m_windows, m_translations));

    if (allWindowsSettled()) {
        finishRun();
        return;
    }
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

    const int retries = (std::max)(0, ConfigManager::instance()->intValue(Keys::documentRetryCount));
    if (m_attempts.at(index) <= retries) {
        const quint64 generation = m_generation;
        QTimer::singleShot(kRetryDelayMs * m_attempts.at(index), this, [this, index, generation]() {
            if (!m_active || generation != m_generation || m_settled.at(index)
                || m_jobs.at(index) != 0)
                return;
            submitWindow(index);
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
        emit failed(m_failedSamples);
        return;
    }
    emit finished(DocumentSegmenter::assemble(m_windows, m_translations));
}
