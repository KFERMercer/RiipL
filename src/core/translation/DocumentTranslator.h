#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include "core/document/DocumentSegmenter.h"
#include "core/network/ApiClient.h"
#include "core/translation/PromptBuilder.h"
#include "core/translation/TranslationEngine.h"

// Sends a document to the API one window at a time and rebuilds the translation
// from the answers, each window carrying its neighbours as context. A window
// that fails is sent again before the run is reported as failed.
class DocumentTranslator : public QObject
{
    Q_OBJECT

public:
    explicit DocumentTranslator(QObject* parent = nullptr);

    void start(const QVector<DocumentWindow>& windows, const TranslationContext& context);
    void stop();

signals:
    // Piece of the window currently in flight.
    void windowStreamed(const QString& piece);
    // The window in flight is being sent again.
    void windowRestarted();
    // Whole translated document, once a window has been accepted.
    void windowTranslated(const QString& text);
    void progressChanged(int completed, int total);
    // The run stops; the windows translated so far are kept.
    void failed(const ApiClient::Error& failure);
    void finished(const QString& text);
    void stopped();

private:
    void translateWindow();
    void handleWindowFinished(const QString& response);
    void retryOrFail(const ApiClient::Error& failure);
    // Drops what the run is doing without reporting it.
    void cancelRun();

    TranslationEngine m_engine;
    QTimer m_retryTimer;
    QVector<DocumentWindow> m_windows;
    // Joined source text of every window, built once per run.
    QVector<QString> m_sources;
    QVector<QStringList> m_translations;
    // Document text of the accepted windows, in order.
    QStringList m_rendered;
    TranslationContext m_context;
    // Requests sent for the window in flight.
    int m_attempts = 0;
    // Holds while a run is under way, including the pauses between retries.
    bool m_active = false;
};
