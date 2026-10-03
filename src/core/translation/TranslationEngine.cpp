#include "TranslationEngine.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "utils/TextUtils.h"

#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace {

// Clause plus this many words on either side reaches the neighbouring sentence,
// which the model needs to judge how the selection fits the translation.
constexpr TextUtils::WordWindow kCandidateContextWords{10, 30};

}

TranslationEngine::TranslationEngine(QObject* parent)
    : QObject(parent)
{
    connect(&m_translateApi, &ApiClient::requestFinished, this, [this]() {
        setBusy(false);
    });
    connect(&m_candidateApi, &ApiClient::requestFinished, this, [this]() {
        if (!m_translateApi.isBusy())
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

bool TranslationEngine::isBusy() const
{
    return m_busy;
}

QJsonObject TranslationEngine::buildRequestBody(const PromptBuilder::Result& prompt, bool stream)
{
    ConfigManager* config = ConfigManager::instance();
    QJsonArray messages;
    if (!prompt.system.isEmpty()) {
        messages.append(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("system")},
            {QStringLiteral("content"), prompt.system}
        });
    }
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), prompt.user}
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
    dispatch(PromptBuilder::build(context));
}

void TranslationEngine::translateDocument(const TranslationContext& context,
                                          const DocumentWindowPrompt& window)
{
    dispatch(PromptBuilder::buildDocument(context, window));
}

void TranslationEngine::dispatch(const PromptBuilder::Result& prompt)
{
    if (m_translateApi.isBusy())
        m_translateApi.cancel();

    if (prompt.user.isEmpty()) {
        emit error({ApiClient::ErrorCode::NothingToTranslate, QString()});
        return;
    }

    QJsonObject body = buildRequestBody(prompt, ConfigManager::instance()->boolValue(Keys::apiStream));

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
            emit partialDelta(delta);
        },
        [this](const ApiClient::Error& failure) {
            emit error(failure);
        });
}

void TranslationEngine::requestCandidates(const TranslationContext& context,
                                          int selectionStart,
                                          int selectionEnd,
                                          const std::function<void(const QVector<CandidateGroup>&)>& onDone,
                                          const std::function<void(const ApiClient::Error&)>& onError)
{
    if (m_candidateApi.isBusy())
        m_candidateApi.cancel();

    // The request carries only the words around the selection, so the prompt
    // stays small no matter how long the document is.
    const TextUtils::Fragment fragment = TextUtils::candidateFragment(
        context.translatedText, selectionStart, selectionEnd, kCandidateContextWords);
    if (!fragment.isValid()) {
        if (onError)
            onError({ApiClient::ErrorCode::NothingToLookUp, QString()});
        return;
    }

    // The selection is wrapped before the window is rendered, so the model sees
    // the marked text inside the fenced block.
    QString marked = fragment.text;
    marked.insert(fragment.markEnd, CandidateMarks::selectionClose);
    marked.insert(fragment.markStart, CandidateMarks::selectionOpen);

    TranslationContext selection = context;
    selection.selectedWord = context.translatedText.mid(selectionStart, selectionEnd - selectionStart);
    selection.selectedFragment = marked;
    // A window already holding every word of the translation is rendered by the
    // template that reaches for the source text. The window never carries the
    // blanks around the translation, so the comparison ignores them too.
    const QString key = fragment.text == context.translatedText.trimmed()
        ? Keys::promptCandidateShort
        : Keys::promptCandidate;
    PromptBuilder::Result prompt;
    prompt.user = PromptBuilder::candidatePrompt(key, selection);
    // Candidates answer under the same system prompt as a translation.
    prompt.system = PromptBuilder::systemPrompt(selection);

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
        [this](const ApiClient::Error& failure) {
            // A transport failure is not worth repeating: the retry targets a
            // reply the model answered without a usable group.
            m_candidateRetryPending = false;
            const auto onError = m_candidateError;
            m_candidateDone = nullptr;
            m_candidateError = nullptr;
            if (onError)
                onError(failure);
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
        QStringList options;
        options.reserve(group.options.size());
        for (const CandidateOption& option : group.options)
            options << option.text;

        const TextUtils::WordSpan span = TextUtils::replacementSpan(
            translatedText, selectionStart, selectionEnd, group.target, options);
        if (!span.isValid())
            continue;
        group.start = span.start;
        group.length = span.length();

        QVector<CandidateOption> aligned;
        aligned.reserve(group.options.size());
        for (const CandidateOption& option : group.options) {
            const TextUtils::WordSpan optionSpan = TextUtils::replacementSpan(
                translatedText, selectionStart, selectionEnd, group.target, {option.text});
            // An option carries its own span because it may absorb characters the
            // target left behind. One that resolves to nothing, or that no
            // longer covers the target, would overwrite the wrong run.
            if (!optionSpan.isValid() || optionSpan.start > span.start
                || optionSpan.start + optionSpan.length() < span.start + span.length())
                continue;
            aligned.append({option.text, optionSpan.start, optionSpan.length()});
        }
        group.options = aligned;
        if (!group.options.isEmpty())
            resolved << group;
    }
    return resolved;
}

QVector<TranslationEngine::CandidateGroup> TranslationEngine::parseCandidateResponse(const QString& raw)
{
    QVector<CandidateGroup> groups;

    const QString text = TextUtils::stripCodeFence(raw);

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
            const bool alreadyListed = std::find_if(
                group.options.cbegin(), group.options.cend(),
                [&text](const CandidateOption& listed) {
                    return listed.text == text;
                }) != group.options.cend();
            if (!text.isEmpty() && text != target && !alreadyListed)
                group.options.append({text, -1, 0});
        }
        if (!group.options.isEmpty())
            groups << group;
    }
    return groups;
}

void TranslationEngine::stop()
{
    if (!m_translateApi.isBusy()) {
        setBusy(false);
        return;
    }
    m_translateApi.cancel();
    emit stopped();
}
