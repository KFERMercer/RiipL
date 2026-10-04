#include "ApiClient.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkRequestFactory>
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
constexpr qsizetype kMaxResponseBytes = 4 * 1024 * 1024;

bool isRequestableScheme(const QUrl& url)
{
    const QString scheme = url.scheme();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

// Message the API worded itself for a failed response.
QString apiErrorMessage(const QByteArray& body)
{
    if (!body.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            const QJsonValue message =
                doc.object().value(QStringLiteral("error")).toObject().value(QStringLiteral("message"));
            if (!message.toString().isEmpty())
                return message.toString();
        }
    }
    return QString();
}

ApiClient::Error streamError(const QByteArray& frame)
{
    const QJsonValue error = QJsonDocument::fromJson(frame).object().value(QStringLiteral("error"));
    const QString message = apiErrorMessage(frame);
    if (!message.isEmpty())
        return {ApiClient::ErrorCode::ServerMessage, message};
    const int code = error.toObject().value(QStringLiteral("code")).toInt();
    if (code > 0)
        return {ApiClient::ErrorCode::HttpStatus, QString::number(code)};
    return {ApiClient::ErrorCode::InvalidResponse, QString()};
}

// Failure a finished reply reports, or nothing when it carried a body the caller
// can read. \p abortedIsFailure is false for a request whose payload arrived
// before the abort.
std::optional<ApiClient::Error> replyFailure(QNetworkReply* reply,
                                            const QByteArray& body,
                                            bool tooLarge,
                                            bool abortedIsFailure)
{
    if (tooLarge)
        return ApiClient::Error{ApiClient::ErrorCode::ResponseTooLarge, QString()};

    const QVariant statusAttribute = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int statusCode = statusAttribute.isValid() ? statusAttribute.toInt() : 0;
    const QNetworkReply::NetworkError replyError = reply->error();

    // The client's own aborts never reach their handlers, so this is the timeout.
    if (replyError == QNetworkReply::OperationCanceledError && abortedIsFailure)
        return ApiClient::Error{ApiClient::ErrorCode::TimedOut, QString()};
    if (replyError != QNetworkReply::NoError && statusCode == 0)
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
                                             const ApiClient::Connection& connection,
                                             const ApiClient::ErrorCallback& onError)
{
    const QString baseUrl = connection.baseUrl.trimmed();
    if (baseUrl.isEmpty()) {
        if (onError)
            onError({ApiClient::ErrorCode::BaseUrlMissing, QString()});
        return std::nullopt;
    }

    // Refused before a request exists: a relative or non-HTTP endpoint cannot
    // carry the key.
    const QUrl endpoint = ApiClient::normalizedBaseUrl(baseUrl);
    if (!endpoint.isValid() || endpoint.isRelative() || endpoint.host().isEmpty()
        || !isRequestableScheme(endpoint)) {
        if (onError)
            onError({ApiClient::ErrorCode::BaseUrlInvalid, QString()});
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
    if (!contentType.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    // Applied last so custom headers can intentionally override built-ins.
    const QHttpHeaders customHeaders = ApiClient::parseCustomHeaders(connection.customHeaders);
    if (!customHeaders.isEmpty()) {
        QHttpHeaders headers = request.headers();
        for (qsizetype i = 0; i < customHeaders.size(); ++i)
            headers.replaceOrAppend(customHeaders.nameAt(i), customHeaders.valueAt(i));
        request.setHeaders(std::move(headers));
    }
    return request;
}

}

ApiClient::ApiClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void ApiClient::cancel()
{
    if (!m_reply)
        return;
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
    m_onDone = nullptr;
    m_onDelta = nullptr;
    m_onError = nullptr;
    emit requestFinished();
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

void ApiClient::sendChatRequest(const QJsonObject& body,
                                DoneCallback onDone,
                                DeltaCallback onStream,
                                ErrorCallback onError)
{
    ConfigManager* config = ConfigManager::instance();

    const std::optional<QNetworkRequest> request = createRequest(
        kChatCompletionsPath, QByteArrayLiteral("application/json"), Connection::configured(), onError);
    if (!request.has_value()) {
        emit requestFinished();
        return;
    }

    QJsonObject payload = body;
    const QString extra = config->stringValue(Keys::apiExtraBody).trimmed();
    if (!extra.isEmpty()) {
        const QJsonDocument extraDoc = QJsonDocument::fromJson(extra.toUtf8());
        if (extraDoc.isObject()) {
            const QJsonObject extraObject = extraDoc.object();
            for (auto it = extraObject.begin(); it != extraObject.end(); ++it)
                payload.insert(it.key(), it.value());
        }
    }

    cancel();

    m_streaming = payload.value(QStringLiteral("stream")).toBool(false);
    m_streamBuffer.clear();
    m_rawBuffer.clear();
    m_accumulated.clear();
    m_receivedBytes = 0;
    m_doneSent = false;
    m_overflowed = false;
    m_streamError.reset();
    m_onDone = std::move(onDone);
    m_onDelta = std::move(onStream);
    m_onError = std::move(onError);

    m_reply = m_nam->post(*request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &ApiClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &ApiClient::onFinished);
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

void ApiClient::listModels(const Connection& connection, ModelsCallback onDone, ErrorCallback onError)
{
    cancelModels();

    const std::optional<QNetworkRequest> request =
        createRequest(kModelsPath, QByteArray(), connection, onError);
    if (!request.has_value())
        return;

    m_modelsBuffer.clear();
    m_modelsOverflowed = false;
    m_modelsOnDone = std::move(onDone);
    m_modelsOnError = std::move(onError);

    m_modelsReply = m_nam->get(*request);
    connect(m_modelsReply, &QNetworkReply::readyRead, this, &ApiClient::onModelsReadyRead);
    connect(m_modelsReply, &QNetworkReply::finished, this, &ApiClient::onModelsFinished);
}

void ApiClient::cancelModels()
{
    if (!m_modelsReply)
        return;
    QNetworkReply* reply = m_modelsReply;
    m_modelsReply = nullptr;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
    m_modelsOnDone = nullptr;
    m_modelsOnError = nullptr;
}

std::optional<QStringList> ApiClient::parseModelIds(const QByteArray& body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return std::nullopt;
    const QJsonValue data = doc.object().value(QStringLiteral("data"));
    if (!data.isArray())
        return std::nullopt;

    QStringList ids;
    for (const QJsonValue& entry : data.toArray()) {
        const QString id = entry.toObject().value(QStringLiteral("id")).toString();
        if (!id.isEmpty())
            ids.append(id);
    }
    return ids;
}

void ApiClient::onModelsReadyRead()
{
    if (!m_modelsReply)
        return;
    m_modelsBuffer += m_modelsReply->readAll();
    if (m_modelsBuffer.size() > kMaxResponseBytes) {
        m_modelsOverflowed = true;
        m_modelsReply->abort();
    }
}

void ApiClient::onModelsFinished()
{
    if (!m_modelsReply)
        return;
    QNetworkReply* reply = m_modelsReply;
    m_modelsReply = nullptr;

    const bool overflowed = std::exchange(m_modelsOverflowed, false);
    // An aborted reply has nothing left to read.
    if (!overflowed && reply->isOpen())
        m_modelsBuffer += reply->readAll();
    const QByteArray body = std::exchange(m_modelsBuffer, QByteArray());
    const bool tooLarge = overflowed || body.size() > kMaxResponseBytes;

    auto done = m_modelsOnDone;
    auto errorCb = m_modelsOnError;
    m_modelsOnDone = nullptr;
    m_modelsOnError = nullptr;
    reply->deleteLater();

    if (const std::optional<Error> failure = replyFailure(reply, body, tooLarge, true)) {
        if (errorCb)
            errorCb(*failure);
        return;
    }

    const std::optional<QStringList> models = parseModelIds(body);
    if (models.has_value()) {
        if (done)
            done(*models);
    } else if (errorCb) {
        errorCb({ErrorCode::InvalidResponse, QString()});
    }
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
    m_rawBuffer += data;
    if (m_streaming) {
        m_streamBuffer += data;
        consumeStreamBuffer(false);
    }
}

void ApiClient::consumeStreamBuffer(bool flush)
{
    int start = 0;
    while (start < m_streamBuffer.size()) {
        int end = m_streamBuffer.indexOf('\n', start);
        if (end < 0) {
            if (!flush)
                break;
            end = m_streamBuffer.size();
        }
        const QByteArray line = m_streamBuffer.mid(start, end - start).trimmed();
        start = end + 1;
        if (line.isEmpty())
            continue;
        if (!line.startsWith("data:"))
            continue;
        const QByteArray payload = line.mid(5).trimmed();
        if (payload == "[DONE]") {
            m_doneSent = true;
            continue;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject())
            continue;
        const QJsonObject object = doc.object();
        // A failure the endpoint reaches mid-answer arrives as a frame of the stream.
        if (object.contains(QStringLiteral("error"))) {
            m_streamError = streamError(payload);
            if (m_reply)
                m_reply->abort();
            return;
        }
        const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty())
            continue;
        const QJsonObject delta = choices.first().toObject().value(QStringLiteral("delta")).toObject();
        const QString piece = delta.value(QStringLiteral("content")).toString();
        if (!piece.isEmpty()) {
            m_accumulated += piece;
            if (m_onDelta)
                m_onDelta(piece);
        }
    }
    if (start > 0)
        m_streamBuffer.remove(0, (std::min)(start, static_cast<int>(m_streamBuffer.size())));
}

void ApiClient::onFinished()
{
    if (!m_reply)
        return;
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;

    const bool overflowed = std::exchange(m_overflowed, false);
    // An aborted reply has nothing left to read.
    const QByteArray remaining =
        !overflowed && reply->isOpen() ? reply->readAll() : QByteArray();
    const bool tooLarge = overflowed || m_receivedBytes + remaining.size() > kMaxResponseBytes;
    if (!tooLarge) {
        m_rawBuffer += remaining;
        if (m_streaming) {
            m_streamBuffer += remaining;
            consumeStreamBuffer(true);
        }
    }

    auto done = m_onDone;
    auto errorCb = m_onError;
    m_onDone = nullptr;
    m_onDelta = nullptr;
    m_onError = nullptr;
    reply->deleteLater();

    // Settled before the notification, so a slot that starts the next request
    // cannot change it.
    QString result;
    std::optional<Error> failure = std::exchange(m_streamError, std::nullopt);
    if (!failure.has_value())
        failure = replyFailure(reply, m_rawBuffer, tooLarge, !m_doneSent);
    if (!failure.has_value()) {
        if (m_streaming) {
            // A stream without content failed like an unusable body, not as an
            // empty translation.
            result = m_accumulated;
            if (result.isEmpty())
                failure = Error{ErrorCode::InvalidResponse, QString()};
        } else {
            const QJsonDocument doc = QJsonDocument::fromJson(m_rawBuffer);
            if (!doc.isObject()) {
                failure = Error{ErrorCode::InvalidResponse, QString()};
            } else {
                const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
                if (choices.isEmpty())
                    failure = Error{ErrorCode::NoChoices, QString()};
                else
                    result = choices.first().toObject()
                                 .value(QStringLiteral("message"))
                                 .toObject()
                                 .value(QStringLiteral("content"))
                                 .toString();
            }
        }
    }

    emit requestFinished();
    if (failure.has_value()) {
        if (errorCb)
            errorCb(*failure);
        return;
    }
    if (done)
        done(result);
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
    case ErrorCode::ResponseTooLarge:
        return tr("API response is too large to accept");
    case ErrorCode::NothingToTranslate:
        return tr("Nothing to translate");
    case ErrorCode::NothingToLookUp:
        return tr("Nothing to look up");
    }
    return QString();
}
