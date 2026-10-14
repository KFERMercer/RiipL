#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

#include "core/document/DocumentCache.h"
#include "core/document/DocumentSegmenter.h"
#include "core/translation/PromptBuilder.h"
#include "core/translation/TranslationPool.h"

namespace DocumentPriority {

inline constexpr int window = 90;
inline constexpr int windowRetry = 89;

}

// Sends a document to the API window by window and rebuilds the translation from
// the answers, each window carrying its neighbours as context. Every window goes
// into the shared translation pool, so an interactive request passes the windows
// still queued. The document is reported again on every accepted window; a window
// that has used its attempts is left out while the run carries on, and a cache
// answers the windows whose request it already holds.
class DocumentTranslator : public QObject
{
    Q_OBJECT

public:
    explicit DocumentTranslator(QObject* parent = nullptr);
    ~DocumentTranslator() override;

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
    DocumentWindowPrompt windowPrompt(int index) const;
    // Name a window is cached and reported under: the digest of its request.
    QString sampleDigest(int index) const;
    // Answer the cache holds for a window, when it still fits the window.
    std::optional<QStringList> cachedTranslation(int index, const QString& sample) const;

    // Hands the windows that may start to the pool. A run translating one window
    // at a time waits for the window in hand to settle first.
    void dispatchPending();
    void submitWindow(int index);
    void handleWindowFinished(int index, const QString& response);
    // A request the endpoint failed, retried while the window has attempts left.
    void handleWindowError(int index);
    void retryOrFail(int index);
    bool allWindowsSettled() const;
    // Reports the end of the run, as failed samples or as the document built.
    void finishRun();
    // Drops what the run is doing without reporting it.
    void cancelRun();
    void cancelJobs();

    QVector<DocumentWindow> m_windows;
    // Joined source text of every window, built once per run.
    QVector<QString> m_sources;
    // Answer of every window, in document order.
    QVector<QStringList> m_translations;
    // Request the pool carries for a window, 0 while none is in flight.
    QVector<TranslationPool::JobId> m_jobs;
    // Digest of the request a window last went out with, and the sends it has used.
    QVector<QString> m_samples;
    QVector<int> m_attempts;
    // Whether a window is accepted or left out, so it is never sent again.
    QVector<bool> m_settled;
    // Windows the run left out, as the short digest of their request.
    QStringList m_failedSamples;
    TranslationContext m_context;
    std::optional<DocumentCache> m_cache = std::nullopt;
    // Next window a run translating one at a time starts.
    int m_nextWindow = 0;
    int m_completed = 0;
    // Holds while a run is under way, including the pauses between retries.
    bool m_active = false;
    // Whether the run keeps one window in hand at a time.
    bool m_serial = false;
    // Bumped whenever a run ends, so the retry a window was waiting out is dropped.
    quint64 m_generation = 0;
};
