#pragma once

#include <QObject>
#include <QString>

#include "core/network/ApiClient.h"
#include "core/translation/PromptBuilder.h"

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

        bool valid() const { return start >= 0 && length > 0; }
    };

    // One replacement target plus the alternatives proposed for it. \p start and
    // \p length locate the target in the translation that was searched.
    struct CandidateGroup
    {
        QString target;
        int start = -1;
        int length = 0;
        QVector<CandidateOption> options;

        bool valid() const { return start >= 0 && length > 0; }
    };

    explicit TranslationEngine(QObject* parent = nullptr);

    void translateText(const TranslationContext& context);
    // Translates one window of a document under the document prompt.
    void translateDocument(const TranslationContext& context,
                           const DocumentWindowPrompt& window);
    void requestCandidates(const TranslationContext& context,
                           int selectionStart,
                           int selectionEnd,
                           const std::function<void(const QVector<CandidateGroup>&)>& onDone,
                           const std::function<void(const ApiClient::Error&)>& onError);
    // Drops the in-flight candidate request and reports nothing back.
    void cancelCandidates();
    void stop();
    bool busy() const;

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
    void partialResult(const QString& text);
    // Piece the latest partial result added, so a longer result can be appended
    // to instead of rewritten.
    void partialDelta(const QString& piece);
    void finished(const QString& text);
    void error(const ApiClient::Error& failure);
    void stopped();
    void stateChanged(bool busy);

private:
    ApiClient m_translateApi;
    ApiClient m_candidateApi;
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
    // Sends one assembled prompt and reports the reply through the signals.
    void dispatch(const PromptBuilder::Result& prompt);
    // Delivers the parsed groups, retrying once when the model answered with
    // nothing usable.
    void deliverCandidates(const QString& raw);
    void dispatchCandidateRequest();
    void resetCandidateState();
};
