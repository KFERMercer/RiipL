#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

#include "core/network/ApiClient.h"
#include "core/translation/PromptBuilder.h"
#include "core/translation/TranslationPool.h"

namespace ApiTemperature {

inline const double providerDefaultSentinel = -0.1;

}

namespace TranslationPriority {

inline constexpr int interactive = 10;
inline constexpr int interactiveRetry = 9;

}

class TranslationEngine : public QObject
{
    Q_OBJECT

public:
    // One alternative wording together with the span of the translation it
    // overwrites. The span sits on the option because a replacement can restate
    // characters the target itself left outside its bounds.
    struct CandidateOption
    {
        QString text;
        int start = -1;
        int length = 0;

        bool isValid() const { return start >= 0 && length > 0; }
    };

    struct CandidateGroup
    {
        QString target;
        int start = -1;
        int length = 0;
        QVector<CandidateOption> options;

        bool isValid() const { return start >= 0 && length > 0; }
    };

    explicit TranslationEngine(QObject* parent = nullptr);
    ~TranslationEngine() override;

    void translateText(const TranslationContext& context);
    void requestCandidates(const TranslationContext& context,
                           int selectionStart,
                           int selectionEnd,
                           const std::function<void(const QVector<CandidateGroup>&)>& onDone,
                           const std::function<void(const ApiClient::Error&)>& onError);
    // Drops the in-flight candidate request and reports nothing back.
    void cancelCandidates();
    void stop();
    bool isBusy() const;

    // Assembles a chat-completions request body from the current configuration.
    // A negative configured temperature omits the parameter from the body.
    static QJsonObject buildRequestBody(const PromptBuilder::Result& prompt, bool stream);

    // Parses the candidate wording reply into its replacement groups, each with
    // its own target, so a model that widens the selection differently per group
    // still resolves. Malformed replies yield no groups.
    static QVector<CandidateGroup> parseCandidateResponse(const QString& raw);

    // Keeps only the groups whose target resolves to exactly one span covering
    // the selection, and records that span plus the span each option overwrites.
    // A target or option that cannot be pinned down is dropped rather than
    // applied to the wrong occurrence.
    static QVector<CandidateGroup> resolveGroups(QVector<CandidateGroup> groups,
                                                 const QString& translatedText,
                                                 int selectionStart,
                                                 int selectionEnd);

signals:
    // Piece the result grew by, so a pane can append instead of rewriting its
    // whole text.
    void partialDelta(const QString& piece);
    void finished(const QString& text);
    void errorOccurred(const ApiClient::Error& failure);
    void stopped();
    void stateChanged(bool busy);

private:
    TranslationPool::JobId m_translateJob = 0;
    TranslationPool::JobId m_candidateJob = 0;
    QString m_accumulated;
    bool m_busy = false;

    // State for the one automatic retry of a candidate request whose reply
    // yielded no group covering the selection; cleared once it is dispatched.
    QJsonObject m_candidateBody;
    QString m_candidateText;
    int m_candidateSelectionStart = -1;
    int m_candidateSelectionEnd = -1;
    std::function<void(const QVector<CandidateGroup>&)> m_candidateDone;
    std::function<void(const ApiClient::Error&)> m_candidateError;
    bool m_candidateRetryPending = false;

    void setBusy(bool busy);
    void updateBusy();
    void dispatch(const PromptBuilder::Result& prompt, int priority);
    void cancelTranslateJob();
    void cancelCandidateJob();
    // Delivers the parsed groups, retrying once when the model answered with
    // nothing usable.
    void deliverCandidates(const QString& raw);
    void dispatchCandidateRequest(int priority);
    void resetCandidateState();
};
