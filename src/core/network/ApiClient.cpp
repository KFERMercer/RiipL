#include "ApiClient.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkRequestFactory>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

namespace {
const QString kChatCompletionsPath = QStringLiteral("/chat/completions");
const QString kModelsPath = QStringLiteral("/models");

// Cap on a response body: a translation never comes close, so a larger body is
// not worth buffering.
constexpr qsizetype kMaxResponseBytes = 1024 * 1024;

bool isRequestableScheme(const QUrl& url)
{
    const QString scheme = url.scheme();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

// Message the API worded itself for a failed response.
QString apiErrorMessage(const QByteArray& body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return QString();
    const QJsonValue error = doc.object().value(QStringLiteral("error"));
    if (error.isString())
        return error.toString();
    const QJsonObject object = error.toObject();
    const QString message = object.value(QStringLiteral("message")).toString();
    if (!message.isEmpty())
        return message;
    // Without its required message, an Error object still names the failure
    // through its code or type.
    const QString code = object.value(QStringLiteral("code")).toString();
    return code.isEmpty() ? object.value(QStringLiteral("type")).toString() : code;
}

struct CompletionReply
{
    QString content;
    std::optional<ApiClient::Error> failure = std::nullopt;
};

std::optional<ApiClient::Error> finishFailure(const QString& finishReason)
{
    if (finishReason == QLatin1String("length"))
        return ApiClient::Error{ApiClient::ErrorCode::Truncated, QString()};
    if (finishReason == QLatin1String("content_filter"))
        return ApiClient::Error{ApiClient::ErrorCode::Filtered, QString()};
    return std::nullopt;
}

// Reads a non-streaming reply as CreateChatCompletionResponse.
CompletionReply parseCompletion(const QByteArray& body)
{
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QJsonValue choices = root.value(QStringLiteral("choices"));
    if (!choices.isArray())
        return {{}, ApiClient::Error{ApiClient::ErrorCode::InvalidResponse, QString()}};
    const QJsonArray array = choices.toArray();
    if (array.isEmpty())
        return {{}, ApiClient::Error{ApiClient::ErrorCode::NoChoices, QString()}};

    const QJsonObject choice = array.first().toObject();
    const QJsonObject message = choice.value(QStringLiteral("message")).toObject();
    const QJsonValue content = message.value(QStringLiteral("content"));
    const std::optional<ApiClient::Error> finish =
        finishFailure(choice.value(QStringLiteral("finish_reason")).toString());
    if (content.isString()) {
        const QString text = content.toString();
        if (!text.isEmpty()) {
            if (finish.has_value())
                return {{}, *finish};
            return {text, std::nullopt};
        }
    } else if (!content.isNull()) {
        // The message content is required, and only text or null is valid.
        return {{}, ApiClient::Error{ApiClient::ErrorCode::InvalidResponse, QString()}};
    }
    const QString refusal = message.value(QStringLiteral("refusal")).toString();
    if (!refusal.isEmpty())
        return {{}, ApiClient::Error{ApiClient::ErrorCode::ServerMessage, refusal}};
    if (finish.has_value())
        return {{}, *finish};
    return {{}, ApiClient::Error{ApiClient::ErrorCode::NoContent, QString()}};
}

// Failure a finished reply reports, or nothing when it carried a body the caller
// can read. \p streamTerminated is set for a stream whose terminator arrived.
std::optional<ApiClient::Error> replyFailure(QNetworkReply* reply,
                                             const QByteArray& body,
                                             bool tooLarge,
                                             bool streamTerminated)
{
    if (tooLarge)
        return ApiClient::Error{ApiClient::ErrorCode::ResponseTooLarge, QString()};

    if (streamTerminated)
        return std::nullopt;

    const QVariant statusAttribute = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int statusCode = statusAttribute.isValid() ? statusAttribute.toInt() : 0;
    const QNetworkReply::NetworkError replyError = reply->error();

    // The client's own aborts are settled above; a cancel or socket timeout left
    // here means the transfer timed out.
    if (replyError == QNetworkReply::OperationCanceledError
        || replyError == QNetworkReply::TimeoutError)
        return ApiClient::Error{ApiClient::ErrorCode::TimedOut, QString()};
    if (replyError != QNetworkReply::NoError && statusCode < 400)
        return ApiClient::Error{ApiClient::ErrorCode::NetworkFailure, reply->errorString()};
    if (replyError == QNetworkReply::NoError && statusCode < 400)
        return std::nullopt;

    const QString serverMessage = apiErrorMessage(body);
    if (!serverMessage.isEmpty())
        return ApiClient::Error{ApiClient::ErrorCode::ServerMessage, serverMessage};
    if (statusCode > 0)
        return ApiClient::Error{ApiClient::ErrorCode::HttpStatus, QString::number(statusCode)};
    return ApiClient::Error{ApiClient::ErrorCode::NetworkFailure, QString()};
}

// Request every endpoint below the base URL shares. The failure is reported,
// and nothing returned, when \p connection cannot carry a request.
std::optional<QNetworkRequest> createRequest(const QString& path,
                                             const QByteArray& contentType,
                                             const QByteArray& accept,
                                             const ApiClient::Connection& connection,
                                             ApiClient::ErrorCode* failure)
{
    const QString baseUrl = connection.baseUrl.trimmed();
    if (baseUrl.isEmpty()) {
        *failure = ApiClient::ErrorCode::BaseUrlMissing;
        return std::nullopt;
    }

    // Refused before a request exists: a relative or non-HTTP endpoint cannot
    // carry the key.
    const QUrl endpoint = ApiClient::normalizedBaseUrl(baseUrl);
    if (!endpoint.isValid() || endpoint.isRelative() || endpoint.host().isEmpty()
        || !isRequestableScheme(endpoint)) {
        *failure = ApiClient::ErrorCode::BaseUrlInvalid;
        return std::nullopt;
    }

    QNetworkRequestFactory factory(endpoint);
    const QString apiKey = connection.apiKey.trimmed();
    if (!apiKey.isEmpty())
        factory.setBearerToken(apiKey.toUtf8());
    // Abort the request when the server exchanges no data within the
    // configured window, covering both connection and idle phases.
    factory.setTransferTimeout(
        std::chrono::milliseconds((std::max)(ApiTimeout::minimumMs, connection.timeoutMs)));
    factory.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkRequest request = factory.createRequest(path);
    QHttpHeaders headers = request.headers();
    if (!contentType.isEmpty())
        headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::ContentType, contentType);
    if (!accept.isEmpty())
        headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::Accept, accept);
    // The shell names the client; the custom headers below may replace it.
    QString userAgent = QCoreApplication::applicationName();
    const QString version = QCoreApplication::applicationVersion();
    if (!version.isEmpty())
        userAgent += QLatin1Char('/') + version;
    if (!userAgent.isEmpty())
        headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::UserAgent, userAgent.toUtf8());
    // Applied last so custom headers can intentionally override built-ins.
    const QHttpHeaders customHeaders = ApiClient::parseCustomHeaders(connection.customHeaders);
    for (qsizetype i = 0; i < customHeaders.size(); ++i)
        headers.replaceOrAppend(customHeaders.nameAt(i), customHeaders.valueAt(i));
    request.setHeaders(std::move(headers));
    return request;
}

}

ApiClient::ApiClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

ApiClient::Connection ApiClient::Connection::configured()
{
    ConfigManager* config = ConfigManager::instance();
    Connection connection;
    connection.baseUrl = config->stringValue(Keys::apiBaseUrl);
    connection.apiKey = config->stringValue(Keys::apiKey);
    connection.customHeaders = config->stringValue(Keys::apiCustomHeaders);
    connection.timeoutMs = config->intValue(Keys::apiTimeoutMs);
    return connection;
}

ApiClient::Connection ApiClient::Connection::fromValues(const QJsonObject& values)
{
    Connection connection;
    connection.baseUrl = values.value(Keys::apiBaseUrl).toString();
    connection.apiKey = values.value(Keys::apiKey).toString();
    connection.customHeaders = values.value(Keys::apiCustomHeaders).toString();
    connection.timeoutMs = values.value(Keys::apiTimeoutMs).toInt();
    return connection;
}

// Splits user-configured multi-line text into raw header pairs. Lines without
// a "Name: value" shape or with an empty header name are skipped.
QHttpHeaders ApiClient::parseCustomHeaders(const QString& raw)
{
    QHttpHeaders headers;
    const QStringList lines = raw.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const qsizetype colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString name = line.left(colon).trimmed();
        if (name.isEmpty())
            continue;
        headers.append(name, line.mid(colon + 1).trimmed());
    }
    return headers;
}

QJsonObject ApiClient::withExtraBody(const QJsonObject& body)
{
    const QString extra = ConfigManager::instance()->stringValue(Keys::apiExtraBody).trimmed();
    if (extra.isEmpty())
        return body;
    const QJsonDocument extraDoc = QJsonDocument::fromJson(extra.toUtf8());
    if (!extraDoc.isObject())
        return body;

    QJsonObject payload = body;
    const QJsonObject extraObject = extraDoc.object();
    for (auto it = extraObject.begin(); it != extraObject.end(); ++it)
        payload.insert(it.key(), it.value());
    return payload;
}

QUrl ApiClient::normalizedBaseUrl(const QString& baseUrl)
{
    // QNetworkRequestFactory appends a relative path as-is, so trailing
    // slashes on a user-supplied base URL are trimmed to avoid doubling them.
    QUrl endpoint(baseUrl);
    QString path = endpoint.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    endpoint.setPath(path);
    return endpoint;
}

std::optional<QList<ApiClient::Model>> ApiClient::parseModels(const QByteArray& body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return std::nullopt;
    const QJsonValue data = doc.object().value(QStringLiteral("data"));
    if (!data.isArray())
        return std::nullopt;

    QList<Model> models;
    for (const QJsonValue& entry : data.toArray()) {
        const QJsonObject object = entry.toObject();
        const QString id = object.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        QString name = object.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            name = object.value(QStringLiteral("display_name")).toString();
        if (name.isEmpty())
            name = object.value(QStringLiteral("displayName")).toString();
        models.append({id, name});
    }
    return models;
}

void ApiClient::sendChatRequest(const QJsonObject& body,
                                DoneCallback onDone,
                                DeltaCallback onStream,
                                ErrorCallback onError)
{
    abortRequest();

    const QJsonObject payload = withExtraBody(body);
    // The specification requires a model and at least one message.
    const QJsonValue model = payload.value(QStringLiteral("model"));
    const QJsonValue messages = payload.value(QStringLiteral("messages"));
    if (!model.isString() || !messages.isArray() || messages.toArray().isEmpty()) {
        emit requestFinished();
        if (onError)
            onError({ErrorCode::InvalidRequest, QString()});
        return;
    }

    const bool streaming = payload.value(QStringLiteral("stream")).toBool(false);
    const QByteArray accept = streaming ? QByteArrayLiteral("text/event-stream")
                                        : QByteArrayLiteral("application/json");
    ErrorCode failure = ErrorCode::NetworkFailure;
    const std::optional<QNetworkRequest> request =
        createRequest(kChatCompletionsPath, QByteArrayLiteral("application/json"), accept,
                      Connection::configured(), &failure);
    if (!request.has_value()) {
        emit requestFinished();
        if (onError)
            onError({failure, QString()});
        return;
    }

    resetRequestState(streaming ? RequestKind::Stream : RequestKind::Chat);
    m_onDone = std::move(onDone);
    m_onDelta = std::move(onStream);
    m_onError = std::move(onError);

    m_reply = m_nam->post(*request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &ApiClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &ApiClient::onFinished);
}

void ApiClient::listModels(const Connection& connection, ModelsCallback onDone, ErrorCallback onError)
{
    abortRequest();

    ErrorCode failure = ErrorCode::NetworkFailure;
    const std::optional<QNetworkRequest> request =
        createRequest(kModelsPath, QByteArray(), QByteArrayLiteral("application/json"), connection,
                      &failure);
    if (!request.has_value()) {
        emit requestFinished();
        if (onError)
            onError({failure, QString()});
        return;
    }

    resetRequestState(RequestKind::Models);
    m_onModels = std::move(onDone);
    m_onError = std::move(onError);

    m_reply = m_nam->get(*request);
    connect(m_reply, &QNetworkReply::readyRead, this, &ApiClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &ApiClient::onFinished);
}

void ApiClient::cancel()
{
    if (!m_reply)
        return;
    abortRequest();
    emit requestFinished();
}

void ApiClient::abortRequest()
{
    if (!m_reply)
        return;
    QNetworkReply* reply = std::exchange(m_reply, nullptr);
    m_kind = RequestKind::None;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
    clearCallbacks();
}

void ApiClient::resetRequestState(RequestKind kind)
{
    m_kind = kind;
    m_body.clear();
    m_receivedBytes = 0;
    m_overflowed = false;
    m_streamBuffer.clear();
    m_streamEventData.clear();
    m_pendingDeltas.clear();
    m_doneSent = false;
    m_streamError.reset();
    m_accumulated.clear();
    m_refusal.clear();
    m_finishReason.clear();
}

void ApiClient::clearCallbacks()
{
    m_onDone = nullptr;
    m_onDelta = nullptr;
    m_onError = nullptr;
    m_onModels = nullptr;
}

void ApiClient::onReadyRead()
{
    if (!m_reply)
        return;
    const QByteArray data = m_reply->readAll();
    m_receivedBytes += data.size();
    if (m_receivedBytes > kMaxResponseBytes) {
        m_overflowed = true;
        m_reply->abort();
        return;
    }
    m_body += data;
    if (m_kind != RequestKind::Stream)
        return;
    m_streamBuffer += data;
    consumeStreamBuffer(false);
    // The terminator or a failure frame ends the stream, so the transfer stops
    // here rather than waiting for the endpoint to close.
    if (m_reply && (m_doneSent || m_streamError.has_value()))
        m_reply->abort();
}

void ApiClient::onFinished()
{
    if (!m_reply)
        return;
    QNetworkReply* reply = std::exchange(m_reply, nullptr);
    const RequestKind kind = std::exchange(m_kind, RequestKind::None);
    const bool overflowed = std::exchange(m_overflowed, false);
    // An aborted reply has nothing left to read.
    const QByteArray remaining = !overflowed && reply->isOpen() ? reply->readAll() : QByteArray();
    const bool tooLarge = overflowed || m_receivedBytes + remaining.size() > kMaxResponseBytes;
    if (!tooLarge)
        m_body += remaining;

    // The transfer's own failure settles first, so bytes left in a broken
    // stream do not turn it into a parse error.
    std::optional<Error> failure = std::exchange(m_streamError, std::nullopt);
    if (!failure.has_value())
        failure = replyFailure(reply, m_body, tooLarge, m_doneSent);
    if (!failure.has_value() && kind == RequestKind::Stream) {
        m_streamBuffer += remaining;
        consumeStreamBuffer(true);
        failure = std::exchange(m_streamError, std::nullopt);
    }

    QString result;
    std::optional<QList<Model>> models;
    if (!failure.has_value()) {
        if (kind == RequestKind::Stream) {
            result = m_accumulated;
            // A refusal carries the model's own reason for producing no text.
            if (result.isEmpty() && !m_refusal.isEmpty()) {
                failure = Error{ErrorCode::ServerMessage, m_refusal};
            } else if (const std::optional<Error> finish = finishFailure(m_finishReason)) {
                failure = finish;
            } else if (result.isEmpty()) {
                failure = Error{ErrorCode::NoContent, QString()};
            }
        } else if (kind == RequestKind::Chat) {
            const CompletionReply completion = parseCompletion(m_body);
            failure = completion.failure;
            if (!failure.has_value())
                result = completion.content;
        } else if (kind == RequestKind::Models) {
            models = parseModels(m_body);
            if (!models.has_value())
                failure = Error{ErrorCode::InvalidResponse, QString()};
        }
    }

    auto done = std::exchange(m_onDone, nullptr);
    auto errorCb = std::exchange(m_onError, nullptr);
    auto modelsDone = std::exchange(m_onModels, nullptr);
    m_onDelta = nullptr;
    reply->deleteLater();

    // Settled before the notification, so a slot that starts the next request
    // cannot change it.
    emit requestFinished();
    if (failure.has_value()) {
        if (errorCb)
            errorCb(*failure);
        return;
    }
    if (kind == RequestKind::Models) {
        if (modelsDone)
            modelsDone(*models);
        return;
    }
    if (done)
        done(result);
}

void ApiClient::consumeStreamBuffer(bool flush)
{
    if (m_streamError.has_value() || m_doneSent)
        return;

    qsizetype start = 0;
    const qsizetype size = m_streamBuffer.size();
    while (start < size) {
        qsizetype lineEnd = -1;
        qsizetype next = -1;
        for (qsizetype at = start; at < size; ++at) {
            const char byte = m_streamBuffer.at(at);
            if (byte == '\n') {
                lineEnd = at;
                next = at + 1;
                break;
            }
            if (byte == '\r') {
                // A CR at the end of the buffer may be half of a CRLF pair.
                if (at + 1 == size && !flush)
                    break;
                lineEnd = at;
                next = at + 1 < size && m_streamBuffer.at(at + 1) == '\n' ? at + 2 : at + 1;
                break;
            }
        }
        if (lineEnd < 0) {
            if (!flush)
                break;
            lineEnd = size;
            next = size;
        }
        consumeStreamLine(QByteArrayView(m_streamBuffer.constData() + start, lineEnd - start));
        start = next;
        if (m_streamError.has_value() || m_doneSent)
            break;
    }
    m_streamBuffer.remove(0, start);

    // Some endpoints close without the blank line that ends the last frame, so a
    // pending event is dispatched once the stream has ended.
    if (flush && !m_streamError.has_value() && !m_doneSent && !m_streamEventData.isEmpty())
        dispatchStreamEvent();
    deliverStreamDeltas();
}

// Runs after the buffer is done, so a slot starting the next request cannot
// disturb the scan in progress.
void ApiClient::deliverStreamDeltas()
{
    if (m_pendingDeltas.isEmpty())
        return;
    const QStringList pieces = std::exchange(m_pendingDeltas, QStringList());
    if (!m_onDelta)
        return;
    const DeltaCallback onDelta = m_onDelta;
    for (const QString& piece : pieces)
        onDelta(piece);
}

// Event stream format: blank-line separated events whose data fields are joined
// with newlines. Chat completions names no event types, so only data is read.
void ApiClient::consumeStreamLine(QByteArrayView line)
{
    if (line.isEmpty()) {
        dispatchStreamEvent();
        return;
    }
    if (line.startsWith(':'))
        return;

    const qsizetype colon = line.indexOf(':');
    const QByteArrayView field = colon < 0 ? line : line.first(colon);
    if (field != QByteArrayView("data"))
        return;

    QByteArrayView value = colon < 0 ? QByteArrayView() : line.sliced(colon + 1);
    if (value.startsWith(' '))
        value = value.sliced(1);
    m_streamEventData.append(value);
    m_streamEventData.append('\n');
}

void ApiClient::dispatchStreamEvent()
{
    if (m_streamEventData.isEmpty())
        return;
    // Every data line appended its newline, including the last one.
    m_streamEventData.chop(1);
    const QByteArray payload = std::exchange(m_streamEventData, QByteArray());
    if (payload == "[DONE]") {
        m_doneSent = true;
        return;
    }
    processStreamPayload(payload);
}

void ApiClient::processStreamPayload(const QByteArray& payload)
{
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) {
        // The specification names only JSON chunks and [DONE] as stream payloads.
        m_streamError = Error{ErrorCode::InvalidResponse, QString()};
        return;
    }
    const QJsonObject chunk = doc.object();
    // A failure the endpoint reaches mid-answer arrives instead of a chunk.
    const QJsonValue error = chunk.value(QStringLiteral("error"));
    if (!error.isUndefined() && !error.isNull()) {
        const QString message = apiErrorMessage(payload);
        m_streamError = message.isEmpty() ? Error{ErrorCode::InvalidResponse, QString()}
                                          : Error{ErrorCode::ServerMessage, message};
        return;
    }
    // A stream that reports usage ends with a chunk without choices.
    const QJsonArray choices = chunk.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return;
    const QJsonObject choice = choices.first().toObject();
    if (const QString finishReason = choice.value(QStringLiteral("finish_reason")).toString();
        !finishReason.isEmpty()) {
        m_finishReason = finishReason;
    }
    const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
    if (const QString refusal = delta.value(QStringLiteral("refusal")).toString();
        !refusal.isEmpty()) {
        m_refusal += refusal;
    }
    const QString piece = delta.value(QStringLiteral("content")).toString();
    if (piece.isEmpty())
        return;
    m_accumulated += piece;
    if (m_onDelta)
        m_pendingDeltas.append(piece);
}

// The failure vocabulary stays with the class that reports it; the engine reuses
// the codes for the failures it detects itself.
QString ApiClient::Error::text() const
{
    switch (code) {
    case ErrorCode::BaseUrlMissing:
        return tr("API base URL is not configured");
    case ErrorCode::BaseUrlInvalid:
        return tr("API base URL must be an absolute http or https URL");
    case ErrorCode::InvalidRequest:
        return tr("Chat request must name a model and at least one message");
    case ErrorCode::TimedOut:
        return tr("Request timed out");
    case ErrorCode::NetworkFailure:
        return detail.isEmpty() ? tr("Network request failed")
                                : tr("Network request failed: %1").arg(detail);
    case ErrorCode::HttpStatus:
        return tr("Request failed with status %1").arg(detail);
    case ErrorCode::ServerMessage:
        // The provider worded this failure itself, so there is no source string.
        return detail;
    case ErrorCode::InvalidResponse:
        return tr("Failed to parse API response");
    case ErrorCode::NoChoices:
        return tr("API response contains no choices");
    case ErrorCode::NoContent:
        return tr("API response contains no text");
    case ErrorCode::Truncated:
        return tr("API response reached the token limit before it ended");
    case ErrorCode::Filtered:
        return tr("API response was stopped by the content filter");
    case ErrorCode::ResponseTooLarge:
        return tr("API response is too large to accept");
    case ErrorCode::NothingToTranslate:
        return tr("Nothing to translate");
    case ErrorCode::NothingToLookUp:
        return tr("Nothing to look up");
    }
    return QString();
}
