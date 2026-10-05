#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <optional>

#include "core/document/DocumentCache.h"
#include "core/document/DocumentSegmenter.h"
#include "core/translation/PromptBuilder.h"
#include "core/translation/TranslationEngine.h"

// Sends a document to the API window by window and rebuilds the translation from
// the answers, each window carrying its neighbours as context. The run keeps a
// configured number of windows in flight and asks for fewer of them when the
// endpoint fails a request. The document is reported again on every accepted
// window; a window that has used its attempts is left out while the run carries
// on, and a cache answers the windows whose request it already holds.
class DocumentTranslator : public QObject
{
    Q_OBJECT

public:
    explicit DocumentTranslator(QObject* parent = nullptr);

    void start(const QVector<DocumentWindow>& windows, const TranslationContext& context,
               std::optional<DocumentCache> cache = std::nullopt);
    void stop();

signals:
    // Whole document as it stands, sent whenever a window is accepted: the
    // windows answered so far carry their translation, the windows still in
    // flight or queued carry their source text. The document order is the
    // document, not the order the answers arrive in.
    void windowTranslated(const QString& text);
    void progressChanged(int completed, int total);
    // The run ends with windows it could not translate, named by the short digest
    // of their request; the windows translated so far are kept.
    void failed(const QStringList& samples);
    void finished(const QString& text);
    void stopped();

private:
    // One request in flight: the engine carrying a window, the digest of the request
    // it carries, the pause before its next attempt and the sends it has used.
    struct Worker
    {
        TranslationEngine* engine = nullptr;
        QTimer* retryTimer = nullptr;
        int window = -1;
        int attempts = 0;
        QString sample;
    };

    // The request one window produces.
    DocumentWindowPrompt windowPrompt(int index) const;
    // Name a window is cached and reported under: the digest of its request, taken
    // where the request is sent.
    QString sampleDigest(int index) const;
    // Answer the cache holds for a window, when it still fits the window.
    std::optional<QStringList> cachedTranslation(int index, const QString& sample) const;

    // Requests a run may have in flight.
    int requestedWorkers() const;
    void ensureWorkers(int count);
    // Hands pending windows to the free workers.
    void dispatchPending();
    void sendWindow(int workerIndex, int window);
    void resendWindow(int workerIndex);
    void handleWindowFinished(int workerIndex, const QString& response);
    // A request the endpoint failed, which lowers what the run keeps in flight.
    void handleWindowError(int workerIndex);
    // Retries a window while it has attempts left, then leaves it out.
    void retryOrFail(int workerIndex);
    // Whether every window is accepted or left out, so nothing is in flight.
    bool allWindowsSettled() const;
    // Reports the end of the run, as failed samples or as the document built.
    void finishRun();
    // Drops what the run is doing without reporting it.
    void cancelRun();

    QVector<Worker> m_workers;
    QVector<DocumentWindow> m_windows;
    // Joined source text of every window, built once per run.
    QVector<QString> m_sources;
    // Answer of every window, in document order; an empty entry is a window the
    // run has not accepted yet.
    QVector<QStringList> m_translations;
    // Windows the run left out, as the short digest of their request.
    QStringList m_failedSamples;
    TranslationContext m_context;
    std::optional<DocumentCache> m_cache = std::nullopt;
    // Next window to hand to a free worker.
    int m_nextWindow = 0;
    // Requests the run may keep in flight, and the ones on the wire.
    int m_inFlightLimit = 1;
    int m_inFlight = 0;
    // Workers holding a window that waits for a free request slot.
    QVector<int> m_waiting;
    int m_completed = 0;
    // Holds while a run is under way, including the pauses between retries.
    bool m_active = false;
    // Keeps a send that reports back on its own stack from dispatching twice.
    bool m_dispatching = false;
};
