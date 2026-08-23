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

    static QStringList cleanCandidates(const QStringList& candidates);
    static CandidateResult parseCandidateResponse(const QString& raw);

signals:
    void partialResult(const QString& text);
    void finished(const QString& text);
    void error(const QString& message);
    void stateChanged(bool busy);

private:
    QJsonObject buildRequestBody(const QString& userContent, bool stream, const QString& systemContent = QString()) const;

    ApiClient m_translateApi;
    ApiClient m_candidateApi;
    QString m_accumulated;
    bool m_busy = false;
    void setBusy(bool busy);
};
