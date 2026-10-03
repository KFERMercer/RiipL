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

#include <chrono>
#include <utility>

namespace {
const QString kChatCompletionsPath = QStringLiteral("/chat/completions");

// Cap on a response body: a translation never comes close, so a larger body is
// not worth buffering.
constexpr qsizetype kMaxResponseBytes = 4 * 1024 * 1024;
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
    m_userCancelled = true;
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

    const QString baseUrl = config->stringValue(Keys::apiBaseUrl).trimmed();
    if (baseUrl.isEmpty()) {
        if (onError)
            onError({ErrorCode::BaseUrlMissing, QString()});
        emit requestFinished();
        return;
    }

    const QString apiKeyValue = config->stringValue(Keys::apiKey).trimmed();

    QNetworkRequestFactory factory{normalizedBaseUrl(baseUrl)};
    if (!apiKeyValue.isEmpty())
        factory.setBearerToken(apiKeyValue.toUtf8());
    // Abort the request when the server exchanges no data within the
    // user-configured window, covering both connection and idle phases.
    factory.setTransferTimeout(std::chrono::milliseconds(qMax(1000, config->intValue(Keys::apiTimeoutMs))));
    factory.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkRequest request = factory.createRequest(kChatCompletionsPath);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // Applied last so custom headers can intentionally override built-ins.
    const QHttpHeaders customHeaders =
        parseCustomHeaders(config->stringValue(Keys::apiCustomHeaders));
    if (!customHeaders.isEmpty()) {
        QHttpHeaders headers = request.headers();
        for (qsizetype i = 0; i < customHeaders.size(); ++i)
            headers.replaceOrAppend(customHeaders.nameAt(i), customHeaders.valueAt(i));
        request.setHeaders(std::move(headers));
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
    m_userCancelled = false;
    m_overflowed = false;
    m_onDone = std::move(onDone);
    m_onDelta = std::move(onStream);
    m_onError = std::move(onError);

    m_reply = m_nam->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &ApiClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &ApiClient::onFinished);
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
        const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
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
        m_streamBuffer.remove(0, qMin(start, int(m_streamBuffer.size())));
}

void ApiClient::onFinished()
{
    if (!m_reply)
        return;
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;

    // An aborted reply has nothing left to read.
    const QByteArray remaining = m_overflowed ? QByteArray() : reply->readAll();
    if (m_receivedBytes + remaining.size() > kMaxResponseBytes) {
        m_overflowed = true;
    } else {
        m_rawBuffer += remaining;
        if (m_streaming) {
            m_streamBuffer += remaining;
            consumeStreamBuffer(true);
        }
    }

    const QVariant statusAttribute = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int statusCode = statusAttribute.isValid() ? statusAttribute.toInt() : 0;
    const QString errorString = reply->errorString();

    auto done = m_onDone;
    auto errorCb = m_onError;
    m_onDone = nullptr;
    m_onDelta = nullptr;
    m_onError = nullptr;
    reply->deleteLater();

    // Reported before the result reaches its callbacks, so a handler that starts
    // the next request is not left reported as idle.
    emit requestFinished();

    if (m_overflowed) {
        m_overflowed = false;
        if (errorCb)
            errorCb({ErrorCode::ResponseTooLarge, QString()});
        return;
    }

    const bool aborted = reply->error() == QNetworkReply::OperationCanceledError;
    if (aborted && !m_doneSent) {
        if (errorCb)
            errorCb({m_userCancelled ? ErrorCode::Cancelled : ErrorCode::TimedOut, QString()});
        return;
    }
    if (reply->error() != QNetworkReply::NoError && statusCode == 0) {
        if (errorCb)
            errorCb({ErrorCode::NetworkFailure, errorString});
        return;
    }
    if (statusCode >= 400 || reply->error() != QNetworkReply::NoError) {
        const QString serverMessage = apiErrorMessage(QString::fromUtf8(m_rawBuffer));
        if (!errorCb)
            return;
        if (!serverMessage.isEmpty())
            errorCb({ErrorCode::ServerMessage, serverMessage});
        else if (statusCode > 0)
            errorCb({ErrorCode::HttpStatus, QString::number(statusCode)});
        else
            errorCb({ErrorCode::NetworkFailure, QString()});
        return;
    }

    QString result = m_accumulated;
    if (m_streaming) {
        // A stream without content failed like an unusable body, not as an empty
        // translation.
        if (result.isEmpty()) {
            if (errorCb)
                errorCb({ErrorCode::InvalidResponse, QString()});
            return;
        }
    } else {
        const QJsonDocument doc = QJsonDocument::fromJson(m_rawBuffer);
        if (!doc.isObject()) {
            if (errorCb)
                errorCb({ErrorCode::InvalidResponse, QString()});
            return;
        }
        const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) {
            if (errorCb)
                errorCb({ErrorCode::NoChoices, QString()});
            return;
        }
        result = choices.first().toObject()
                     .value(QStringLiteral("message"))
                     .toObject()
                     .value(QStringLiteral("content"))
                     .toString();
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
    case ErrorCode::Cancelled:
        return tr("Translation cancelled");
    case ErrorCode::TimedOut:
        return tr("Translation timed out");
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

QString ApiClient::apiErrorMessage(const QString& body) const
{
    if (!body.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8());
        if (doc.isObject()) {
            const QJsonValue message = doc.object().value(QStringLiteral("error")).toObject().value(QStringLiteral("message"));
            if (!message.toString().isEmpty())
                return message.toString();
        }
    }
    return QString();
}
