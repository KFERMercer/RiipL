#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include "core/document/DocumentSegmenter.h"
#include "core/network/ApiClient.h"
#include "core/translation/PromptBuilder.h"
#include "core/translation/TranslationEngine.h"

// Sends a document to the API window by window and rebuilds the translation from
// the answers, each window carrying its neighbours as context. Windows are in
// flight up to the configured limit, and the document is reported again whenever
// one of them is accepted; a window that fails is sent again before the run is
// reported as failed.
class DocumentTranslator : public QObject
{
    Q_OBJECT

public:
    explicit DocumentTranslator(QObject* parent = nullptr);

    void start(const QVector<DocumentWindow>& windows, const TranslationContext& context);
    void stop();

signals:
    // Whole document as it stands, sent whenever a window is accepted: the
    // windows answered so far carry their translation, the windows still in
    // flight or queued carry their source text. The document order is the
    // document, not the order the answers arrive in.
    void windowTranslated(const QString& text);
    void progressChanged(int completed, int total);
    // The run stops; the windows translated so far are kept.
    void failed(const ApiClient::Error& failure);
    void finished(const QString& text);
    void stopped();

private:
    // One request in flight: the engine carrying a window, the pause before its
    // next attempt and the sends the window has used.
    struct Worker
    {
        TranslationEngine* engine = nullptr;
        QTimer* retryTimer = nullptr;
        int window = -1;
        int attempts = 0;
    };

    // Requests a run may have in flight.
    int requestedWorkers() const;
    void ensureWorkers(int count);
    // Hands pending windows to the free workers.
    void dispatchPending();
    void sendWindow(int workerIndex, int window);
    void resendWindow(int workerIndex);
    void handleWindowFinished(int workerIndex, const QString& response);
    void retryOrFail(int workerIndex, const ApiClient::Error& failure);
    // Drops what the run is doing without reporting it.
    void cancelRun();

    QVector<Worker> m_workers;
    QVector<DocumentWindow> m_windows;
    // Joined source text of every window, built once per run.
    QVector<QString> m_sources;
    // Answer of every window, in document order; an empty entry is a window the
    // run has not accepted yet.
    QVector<QStringList> m_translations;
    TranslationContext m_context;
    // Next window to hand to a free worker.
    int m_nextWindow = 0;
    int m_completed = 0;
    // Holds while a run is under way, including the pauses between retries.
    bool m_active = false;
};
