#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "core/network/ApiClient.h"
#include "core/translation/PromptBuilder.h"

class TranslationEngine : public QObject
{
    Q_OBJECT

public:
    struct CandidateResult
    {
        QString replaceTarget;
        QStringList options;
    };

    explicit TranslationEngine(QObject* parent = nullptr);

    void translateText(const TranslationContext& context);
    void requestCandidates(const QString& sourceText,
                           const QString& translatedText,
                           const QString& word,
                           const QString& targetLang,
                           const std::function<void(const CandidateResult&)>& onDone,
                           const std::function<void(const QString&)>& onError);
    void stop();
    bool busy() const;

    // Assembles a chat-completions request body from the current configuration.
    // A negative configured temperature omits the parameter from the body.
    static QJsonObject buildRequestBody(const QString& userContent, bool stream,
                                        const QString& systemContent = QString());

    static QStringList cleanCandidates(const QStringList& candidates);
    static CandidateResult parseCandidateResponse(const QString& raw);

signals:
    void partialResult(const QString& text);
    void finished(const QString& text);
    void error(const QString& message);
    void stateChanged(bool busy);

private:
    ApiClient m_translateApi;
    ApiClient m_candidateApi;
    QString m_accumulated;
    bool m_busy = false;
    void setBusy(bool busy);
};
