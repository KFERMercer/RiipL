#include "TranslationEngine.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "utils/TextUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// Sentence plus this many characters on either side reaches the neighbouring
// clause, which the model needs to judge how the selection fits the sentence.
constexpr int kCandidateContextChars = 80;

}

TranslationEngine::TranslationEngine(QObject* parent)
    : QObject(parent)
{
    connect(&m_translateApi, &ApiClient::requestFinished, this, [this]() {
        setBusy(false);
    });
    connect(&m_candidateApi, &ApiClient::requestFinished, this, [this]() {
        if (!m_translateApi.busy())
            setBusy(false);
    });
}

void TranslationEngine::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit stateChanged(m_busy);
}

bool TranslationEngine::busy() const
{
    return m_busy;
}

QJsonObject TranslationEngine::buildRequestBody(const QString& userContent, bool stream, const QString& systemContent)
{
    ConfigManager* config = ConfigManager::instance();
    QJsonArray messages;
    if (!systemContent.isEmpty()) {
        messages.append(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("system")},
            {QStringLiteral("content"), systemContent}
        });
    }
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), userContent}
    });

    QJsonObject body;
    body.insert(QStringLiteral("model"), config->stringValue(Keys::apiModel));
    body.insert(QStringLiteral("messages"), messages);
    // A negative configured temperature leaves sampling to the provider default.
    const double temperature = config->doubleValue(Keys::apiTemperature);
    if (temperature >= 0.0)
        body.insert(QStringLiteral("temperature"), temperature);
    body.insert(QStringLiteral("max_tokens"), config->intValue(Keys::apiMaxTokens));
    body.insert(QStringLiteral("stream"), stream);
    return body;
}

void TranslationEngine::translateText(const TranslationContext& context)
{
    if (m_translateApi.busy())
        m_translateApi.cancel();

    const PromptBuilder::Result prompt = PromptBuilder::build(context);
    if (prompt.user.isEmpty()) {
        emit error(tr("Nothing to translate"));
        return;
    }

    QJsonObject body = buildRequestBody(prompt.user, ConfigManager::instance()->boolValue(Keys::apiStream), prompt.system);

    m_accumulated.clear();
    setBusy(true);

    m_translateApi.sendChatRequest(body,
        [this](const QString& result) {
            const QString text = result.isEmpty() ? m_accumulated : result;
            emit finished(text.trimmed());
        },
        [this](const QString& delta) {
            m_accumulated += delta;
            emit partialResult(m_accumulated);
        },
        [this](const QString& message) {
            emit error(message);
        });
}

void TranslationEngine::requestCandidates(const TranslationContext& context,
                                          int selectionStart,
                                          int selectionEnd,
                                          const std::function<void(const QVector<CandidateGroup>&)>& onDone,
                                          const std::function<void(const QString&)>& onError)
{
    if (m_candidateApi.busy())
        m_candidateApi.cancel();

    // The request carries only the sentence around the selection, so the prompt
    // stays small no matter how long the document is.
    const TextUtils::Fragment fragment = TextUtils::candidateFragment(
        context.translatedText, selectionStart, selectionEnd, kCandidateContextChars);
    if (!fragment.valid()) {
        if (onError)
            onError(tr("Nothing to look up"));
        return;
    }

    // The selection is wrapped before the fragment is rendered, so the model
    // sees the marked text inside the fenced block.
    QString marked = fragment.text;
    marked.insert(fragment.markEnd, CandidateMarks::selectionClose);
    marked.insert(fragment.markStart, CandidateMarks::selectionOpen);

    const QString word = context.translatedText.mid(selectionStart, selectionEnd - selectionStart);
    const QString uiLanguage = ConfigManager::instance()->resolvedUiLanguage();
    const QString prompt = PromptBuilder::candidatePrompt(
        context.translatedText, marked, word, context.targetLang, uiLanguage);

    m_candidateBody = buildRequestBody(prompt, false);
    m_candidateText = context.translatedText;
    m_candidateSelectionStart = selectionStart;
    m_candidateSelectionEnd = selectionEnd;
    m_candidateDone = onDone;
    m_candidateError = onError;
    // One retry is allowed per request; the flag is cleared as soon as it is
    // spent, so a model that keeps answering unusably cannot loop.
    m_candidateRetryPending = true;
    dispatchCandidateRequest();
}

void TranslationEngine::cancelCandidates()
{
    resetCandidateState();
    m_candidateApi.cancel();
}

void TranslationEngine::dispatchCandidateRequest()
{
    m_candidateApi.sendChatRequest(m_candidateBody,
        [this](const QString& result) {
            deliverCandidates(result);
        },
        {},
        [this](const QString& message) {
            // A transport failure is not worth repeating: the retry targets a
            // reply the model answered without a usable group.
            m_candidateRetryPending = false;
            const auto onError = m_candidateError;
            m_candidateDone = nullptr;
            m_candidateError = nullptr;
            if (onError)
                onError(message);
        });
}

void TranslationEngine::deliverCandidates(const QString& raw)
{
    const QVector<CandidateGroup> groups = resolveGroups(
        parseCandidateResponse(raw), m_candidateText,
        m_candidateSelectionStart, m_candidateSelectionEnd);

    if (groups.isEmpty() && m_candidateRetryPending) {
        m_candidateRetryPending = false;
        dispatchCandidateRequest();
        return;
    }

    const auto onDone = m_candidateDone;
    resetCandidateState();
    if (onDone)
        onDone(groups);
}

void TranslationEngine::resetCandidateState()
{
    m_candidateRetryPending = false;
    m_candidateDone = nullptr;
    m_candidateError = nullptr;
    m_candidateBody = QJsonObject();
    m_candidateText.clear();
    m_candidateSelectionStart = -1;
    m_candidateSelectionEnd = -1;
}

QVector<TranslationEngine::CandidateGroup> TranslationEngine::resolveGroups(
    QVector<CandidateGroup> groups, const QString& translatedText,
    int selectionStart, int selectionEnd)
{
    QVector<CandidateGroup> resolved;
    resolved.reserve(groups.size());
    for (CandidateGroup& group : groups) {
        const TextUtils::WordSpan span = TextUtils::resolveCandidate(
            translatedText, selectionStart, selectionEnd, group.target);
        if (!span.valid())
            continue;
        group.start = span.start;
        group.length = span.length();
        resolved << group;
    }
    return resolved;
}

QVector<TranslationEngine::CandidateGroup> TranslationEngine::parseCandidateResponse(const QString& raw)
{
    QVector<CandidateGroup> groups;

    QString text = raw.trimmed();
    if (text.startsWith(QStringLiteral("```"))) {
        const int firstNewline = text.indexOf(QLatin1Char('\n'));
        if (firstNewline != -1)
            text = text.mid(firstNewline + 1);
        const int fenceEnd = text.lastIndexOf(QStringLiteral("```"));
        if (fenceEnd != -1)
            text = text.left(fenceEnd);
        text = text.trimmed();
    }

    const int arrayStart = text.indexOf(QLatin1Char('['));
    const int arrayEnd = text.lastIndexOf(QLatin1Char(']'));
    if (arrayStart == -1 || arrayEnd <= arrayStart)
        return groups;

    const QJsonDocument document = QJsonDocument::fromJson(
        text.mid(arrayStart, arrayEnd - arrayStart + 1).toUtf8());
    if (!document.isArray())
        return groups;

    for (const QJsonValue& value : document.array()) {
        const QJsonObject object = value.toObject();
        const QString target = object.value(QStringLiteral("old")).toString().trimmed();
        if (target.isEmpty())
            continue;
        CandidateGroup group;
        group.target = target;
        const QJsonValue options = object.value(QStringLiteral("new"));
        const QJsonArray array = options.isArray() ? options.toArray()
                                                   : QJsonArray{options};
        for (const QJsonValue& option : array) {
            const QString text = option.toString().trimmed();
            if (!text.isEmpty() && text != target && !group.options.contains(text))
                group.options << text;
        }
        if (!group.options.isEmpty())
            groups << group;
    }
    return groups;
}

void TranslationEngine::stop()
{
    if (!m_translateApi.busy()) {
        setBusy(false);
        return;
    }
    m_translateApi.cancel();
    emit stopped();
}
