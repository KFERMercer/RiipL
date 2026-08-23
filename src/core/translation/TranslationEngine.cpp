#include "TranslationEngine.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

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

QJsonObject TranslationEngine::buildRequestBody(const QString& userContent, bool stream) const
{
    ConfigManager* config = ConfigManager::instance();
    QJsonArray messages;
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), userContent}
    });

    QJsonObject body;
    body.insert(QStringLiteral("model"), config->stringValue(Keys::apiModel));
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("temperature"), config->doubleValue(Keys::apiTemperature));
    body.insert(QStringLiteral("max_tokens"), config->intValue(Keys::apiMaxTokens));
    body.insert(QStringLiteral("top_p"), config->doubleValue(Keys::apiTopP));
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

    QJsonObject body = buildRequestBody(prompt.user, ConfigManager::instance()->boolValue(Keys::apiStream));

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

void TranslationEngine::requestCandidates(const QString& sourceText,
                                          const QString& translatedText,
                                          const QString& word,
                                          const QString& targetLang,
                                          const std::function<void(const CandidateResult&)>& onDone,
                                          const std::function<void(const QString&)>& onError)
{
    if (m_candidateApi.busy())
        m_candidateApi.cancel();

    const QString uiLanguage = ConfigManager::instance()->resolvedUiLanguage();
    const QString prompt = PromptBuilder::candidatePrompt(sourceText, translatedText, word, targetLang, uiLanguage);
    QJsonObject body = buildRequestBody(prompt, false);

    m_candidateApi.sendChatRequest(body,
        [this, onDone](const QString& result) {
            if (onDone)
                onDone(parseCandidateResponse(result));
        },
        {},
        [onError](const QString& message) {
            if (onError)
                onError(message);
        });
}

TranslationEngine::CandidateResult TranslationEngine::parseCandidateResponse(const QString& raw)
{
    CandidateResult result;

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

    const int braceStart = text.indexOf(QLatin1Char('{'));
    const int braceEnd = text.lastIndexOf(QLatin1Char('}'));
    if (braceStart != -1 && braceEnd > braceStart) {
        const QJsonDocument doc = QJsonDocument::fromJson(text.mid(braceStart, braceEnd - braceStart + 1).toUtf8());
        if (doc.isObject()) {
            const QJsonObject object = doc.object();
            result.replaceTarget = object.value(QStringLiteral("replace")).toString().trimmed();
            const QJsonArray options = object.value(QStringLiteral("options")).toArray();
            for (const QJsonValue& value : options) {
                const QString option = value.toString().trimmed();
                if (!option.isEmpty())
                    result.options << option;
            }
            if (!result.options.isEmpty())
                return result;
            result.replaceTarget.clear();
        }
    }

    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    result.options = cleanCandidates(lines);
    while (result.options.size() > 6)
        result.options.removeLast();
    return result;
}

QStringList TranslationEngine::cleanCandidates(const QStringList& candidates)
{
    static const QRegularExpression bulletPattern(QStringLiteral("^\\s*(?:[-*\u2022\u2023]\\s+|\\d{1,2}[.)]\\s+)"));
    static const QRegularExpression leadingQuotes(QStringLiteral("^[`'\"]+"));
    static const QRegularExpression trailingQuotes(QStringLiteral("[`'\"]+$"));

    QStringList cleaned;
    cleaned.reserve(candidates.size());
    for (const QString& candidate : candidates) {
        QString text = candidate.trimmed();
        text.remove(bulletPattern);
        text.remove(leadingQuotes);
        text.remove(trailingQuotes);
        for (int i = 0; i < 2; ++i) {
            if (text.size() >= 2) {
                const QChar first = text.front();
                if (first == text.back() && (first == QLatin1Char('`') || first == QLatin1Char('\"') || first == QLatin1Char('\'')))
                    text = text.mid(1, text.size() - 2).trimmed();
            }
        }
        if (!text.isEmpty())
            cleaned << text;
    }
    return cleaned;
}

void TranslationEngine::stop()
{
    if (m_translateApi.busy())
        m_translateApi.cancel();
    else
        setBusy(false);
}
