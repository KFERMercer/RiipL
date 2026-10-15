#include "TranslationPool.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <utility>

namespace {

// Any configured count reaches one within this many halvings.
constexpr int kMaxHalvings = 30;

}

TranslationPool* TranslationPool::instance()
{
    // The pool follows the application, so a call after it is gone builds a new
    // one rather than reaching through a stale pointer.
    static QPointer<TranslationPool> pool;
    if (!pool)
        pool = new TranslationPool(QCoreApplication::instance());
    return pool;
}

TranslationPool::TranslationPool(QObject* parent)
    : QObject(parent)
{
}

int TranslationPool::limit() const
{
    ConfigManager* config = ConfigManager::instance();
    const int configured = config ? (std::max)(1, config->intValue(Keys::apiMaxConcurrency)) : 1;
    return (std::max)(1, configured >> m_halvings);
}

bool TranslationPool::isBusy() const
{
    return !m_pending.isEmpty() || !m_clientJobs.isEmpty();
}

void TranslationPool::updateBusy()
{
    const bool busy = isBusy();
    if (busy == m_busy)
        return;
    m_busy = busy;
    emit busyChanged(busy);
}

void TranslationPool::noteSuccess()
{
    if (m_halvings == 0 || !m_filledSinceFailure)
        return;
    m_halvings = 0;
    m_filledSinceFailure = false;
    // The restored count may let the queue in.
    scheduleDispatch();
}

void TranslationPool::noteFailure()
{
    m_halvings = (std::min)(m_halvings + 1, kMaxHalvings);
    m_filledSinceFailure = false;
}

TranslationPool::JobId TranslationPool::enqueue(const QJsonObject& body,
                                                int priority,
                                                DoneCallback onDone,
                                                DeltaCallback onDelta,
                                                ErrorCallback onError)
{
    const JobId id = m_nextId++;
    const QueueKey key{priority, m_sequence++};
    Job job;
    job.id = id;
    job.body = body;
    job.onDone = std::move(onDone);
    job.onDelta = std::move(onDelta);
    job.onError = std::move(onError);

    m_pending.insert(key, std::move(job));
    m_pendingKeys.insert(id, key);
    updateBusy();
    scheduleDispatch();
    return id;
}

void TranslationPool::cancel(JobId id)
{
    if (id == 0)
        return;

    if (const auto queued = m_pendingKeys.constFind(id); queued != m_pendingKeys.constEnd()) {
        m_pending.remove(queued.value());
        m_pendingKeys.erase(queued);
        updateBusy();
        return;
    }

    // A running request gives its connection back when the client reports the
    // cancel, which is where the pool learns it is no longer busy.
    if (ApiClient* running = m_jobClients.value(id, nullptr))
        running->cancel();
}

void TranslationPool::reset()
{
    m_halvings = 0;
    m_filledSinceFailure = false;
}

void TranslationPool::scheduleDispatch()
{
    if (m_dispatchScheduled)
        return;
    m_dispatchScheduled = QMetaObject::invokeMethod(this, &TranslationPool::dispatch,
                                                    Qt::QueuedConnection);
}

void TranslationPool::dispatch()
{
    m_dispatchScheduled = false;
    while (m_clientJobs.size() < limit() && !m_pending.isEmpty()) {
        auto next = m_pending.begin();
        Job job = next.value();
        m_pendingKeys.remove(job.id);
        m_pending.erase(next);

        ApiClient* client = acquireClient();
        m_clientJobs.insert(client, job.id);
        m_jobClients.insert(job.id, client);
        if (m_clientJobs.size() >= limit())
            m_filledSinceFailure = true;
        // Observed here so the back-off follows every request the pool carries.
        client->sendChatRequest(job.body,
            [this, done = std::move(job.onDone)](const QString& result) {
                noteSuccess();
                if (done)
                    done(result);
            },
            std::move(job.onDelta),
            [this, error = std::move(job.onError)](const ApiClient::Error& failure) {
                noteFailure();
                emit requestFailed();
                if (error)
                    error(failure);
            });
    }
    updateBusy();
}

ApiClient* TranslationPool::acquireClient()
{
    // Only reached below the limit, so the pool holds no more connections than
    // the configured concurrency.
    for (ApiClient* client : std::as_const(m_clients)) {
        if (!m_clientJobs.contains(client))
            return client;
    }
    auto* client = new ApiClient(this);
    connect(client, &ApiClient::requestFinished, this,
            [this, client]() { onClientFinished(client); });
    m_clients.append(client);
    return client;
}

void TranslationPool::onClientFinished(ApiClient* client)
{
    m_jobClients.remove(m_clientJobs.take(client));
    updateBusy();
    scheduleDispatch();
}
