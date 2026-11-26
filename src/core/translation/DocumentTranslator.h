#pragma once

#include <QJsonObject>
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

namespace DocumentRetry {

// Pause before each attempt after the first, multiplied by the attempt number.
inline constexpr int delayMs = 1000;

}

struct DocumentRunOptions
{
    int priority = DocumentPriority::window;
    int priorityRetry = DocumentPriority::windowRetry;
    // Attempts after the first; unset takes the configured document count.
    std::optional<int> retryCount = std::nullopt;
    // Callers may shorten it; zero still leaves the retry on the event loop.
    int retryDelayMs = DocumentRetry::delayMs;
};

// Sends a document to the API window by window, each window carrying its neighbours
// as context, and reports each window as soon as its answer lands, whatever place
// the window holds. A window goes into the shared translation pool as the run takes
// it, so an interactive request passes the windows still queued. A window that has
// used its attempts is left out while the run carries on, and a cache answers the
// windows whose request it already holds.
class DocumentTranslator : public QObject
{
    Q_OBJECT

public:
    explicit DocumentTranslator(QObject* parent = nullptr);
    ~DocumentTranslator() override;

    void start(const QVector<DocumentWindow>& windows, const TranslationContext& context,
               std::optional<DocumentCache> cache = std::nullopt,
               const DocumentRunOptions& options = DocumentRunOptions{});
    void stop();
    bool isRunning() const { return m_active; }
    // Document as the run has it: every window answered, the rest as their source
    // text. Built on demand, so a run that ends early can still be exported whole.
    QString document() const;

signals:
    // One window as it reads in the document, whitespace included, sent once as soon
    // as the window settles and in whatever order the answers arrive, so a consumer
    // places it by \p index. A window the run left out carries its source text.
    void windowTranslated(int index, const QString& text);
    void progressChanged(int completed, int total);
    // The run ends with windows it could not translate, named by the short digest
    // of their request; the windows translated so far are kept.
    void failed(const QStringList& samples);
    void finished(const QString& text);
    void stopped();

private:
    struct WindowRequest
    {
        QJsonObject body;
        QString digest;
    };

    DocumentWindowPrompt windowPrompt(int index) const;
    WindowRequest buildRequest(int index) const;
    // Answer the cache holds for a window, when it still fits the window.
    std::optional<QStringList> cachedTranslation(int index, const QString& digest) const;

    // Hands the next window to the pool. A run translating one window at a time
    // waits for the window in hand to settle first.
    void dispatchPending();
    void processWindow(int index);
    void sendWindow(int index, const WindowRequest& request);
    void acceptWindow(int index, const QStringList& lines);
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
    DocumentRunOptions m_options;
    // Resolved from the run options when the run starts.
    int m_retries = 0;
    int m_retryDelayMs = DocumentRetry::delayMs;
    std::optional<DocumentCache> m_cache = std::nullopt;
    // Next window the run has not taken.
    int m_nextWindow = 0;
    int m_completed = 0;
    // Holds while a run is under way, including the pauses between retries.
    bool m_active = false;
    // Whether the run keeps one window in hand at a time.
    bool m_serial = false;
    // Bumped whenever a run ends: a retry a window was waiting out is dropped, and
    // every path that reaches a slot compares it, since the slot may stop or replace
    // the run.
    quint64 m_generation = 0;
};
