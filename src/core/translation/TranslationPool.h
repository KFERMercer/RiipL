#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QVector>

#include <utility>

#include "core/network/ApiClient.h"

// Carries every translation request over one bounded set of connections, so no
// action pushes the number of requests past the configured concurrency. A request
// waits with the priority its caller gives it and the next free connection takes
// the lowest one. A failed request halves the concurrency the pool allows, and
// once the reduced width has been filled and answered twice one halving is given
// back. A failure after that halves the width from where it stands.
class TranslationPool : public QObject
{
    Q_OBJECT

public:
    using JobId = quint64;
    using DoneCallback = ApiClient::DoneCallback;
    using DeltaCallback = ApiClient::DeltaCallback;
    using ErrorCallback = ApiClient::ErrorCallback;

    static TranslationPool* instance();

    explicit TranslationPool(QObject* parent = nullptr);

    // Queues one chat request under \p priority, a number from 0 to 99 where the
    // lower one runs first, and returns the handle that cancels it. The callbacks
    // never run before this returns.
    JobId enqueue(const QJsonObject& body,
                  int priority,
                  DoneCallback onDone,
                  DeltaCallback onDelta,
                  ErrorCallback onError);
    // Drops a queued or running request, reporting nothing to its callbacks.
    void cancel(JobId id);
    // Forgets the back-off learned from failed requests.
    void reset();
    // Whether the pool holds a request, queued or running.
    bool isBusy() const;

signals:
    // Whether the pool carries requests, when that changes.
    void busyChanged(bool busy);
    void requestFailed();

private:
    struct Job
    {
        JobId id = 0;
        QJsonObject body;
        DoneCallback onDone;
        DeltaCallback onDelta;
        ErrorCallback onError;
    };

    // Arrival order breaks a priority tie.
    using QueueKey = std::pair<int, quint64>;

    void dispatch();
    void scheduleDispatch();
    void onClientFinished(ApiClient* client);
    ApiClient* acquireClient();
    // Connections requests may run on, after any back-off.
    int limit() const;
    int configuredConcurrency() const;
    void noteSuccess();
    void noteFailure();
    void updateBusy();

    QVector<ApiClient*> m_clients;
    QHash<ApiClient*, JobId> m_clientJobs;
    QHash<JobId, ApiClient*> m_jobClients;
    QMap<QueueKey, Job> m_pending;
    QHash<JobId, QueueKey> m_pendingKeys;
    JobId m_nextId = 1;
    quint64 m_sequence = 0;
    // Halvings applied by the current back-off.
    int m_halvings = 0;
    // Whether the reduced width has been filled since the back-off last changed.
    bool m_widthFilled = false;
    int m_successesWhileFilled = 0;
    bool m_dispatchScheduled = false;
    bool m_busy = false;
};
