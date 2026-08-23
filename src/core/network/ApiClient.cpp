#include "ApiClient.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace {
constexpr int kIdleTimeoutMs = 60000;
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

void ApiClient::sendChatRequest(const QJsonObject& body,
                                DoneCallback onDone,
                                DeltaCallback onStream,
                                ErrorCallback onError)
{
    ConfigManager* config = ConfigManager::instance();

    const QString baseUrl = config->stringValue(Keys::apiBaseUrl).trimmed();
    if (baseUrl.isEmpty()) {
        if (onError)
            onError(tr("API base URL is not configured"));
        return;
    }
    const QString apiKeyValue = config->stringValue(Keys::apiKey).trimmed();

    QString url = baseUrl;
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    url += QStringLiteral("/chat/completions");

    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!apiKeyValue.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + apiKeyValue.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

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
    m_doneSent = false;
    m_idleTimedOut = false;
    m_onDone = std::move(onDone);
    m_onDelta = std::move(onStream);
    m_onError = std::move(onError);

    m_reply = m_nam->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &ApiClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &ApiClient::onFinished);

    QTimer* idleTimer = new QTimer(m_reply);
    idleTimer->setSingleShot(true);
    idleTimer->setInterval(kIdleTimeoutMs);
    QNetworkReply* watchedReply = m_reply;
    connect(idleTimer, &QTimer::timeout, this, [this, watchedReply]() {
        if (m_reply == watchedReply) {
            m_idleTimedOut = true;
            watchedReply->abort();
        }
    });
    connect(m_reply, &QNetworkReply::readyRead, idleTimer, [idleTimer]() { idleTimer->start(); });
    idleTimer->start();
}

void ApiClient::onReadyRead()
{
    if (!m_reply)
        return;
    const QByteArray data = m_reply->readAll();
    m_rawBuffer += data;
    if (m_streaming) {
        m_streamBuffer += data;
        consumeStreamBuffer();
    }
}

void ApiClient::consumeStreamBuffer()
{
    int start = 0;
    while (true) {
        const int index = m_streamBuffer.indexOf('\n', start);
        if (index < 0)
            break;
        const QByteArray line = m_streamBuffer.mid(start, index - start).trimmed();
        start = index + 1;
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
        m_streamBuffer.remove(0, start);
}

void ApiClient::onFinished()
{
    if (!m_reply)
        return;
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;

    const QByteArray remaining = reply->readAll();
    m_rawBuffer += remaining;
    if (m_streaming && !remaining.isEmpty()) {
        m_streamBuffer += remaining;
        consumeStreamBuffer();
        m_streamBuffer.clear();
    }

    const QVariant statusAttribute = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int statusCode = statusAttribute.isValid() ? statusAttribute.toInt() : 0;
    const QString errorString = reply->errorString();

    auto done = m_onDone;
    auto deltaCb = m_onDelta;
    auto errorCb = m_onError;
    m_onDone = nullptr;
    m_onDelta = nullptr;
    m_onError = nullptr;
    reply->deleteLater();

    const bool aborted = reply->error() == QNetworkReply::OperationCanceledError;
    if (aborted && !m_doneSent) {
        if (errorCb)
            errorCb(m_idleTimedOut ? tr("Translation timed out") : tr("Translation cancelled"));
        emit requestFinished();
        return;
    }
    if (reply->error() != QNetworkReply::NoError && statusCode == 0) {
        if (errorCb)
            errorCb(tr("Network request failed: %1").arg(errorString));
        emit requestFinished();
        return;
    }
    if (statusCode >= 400 || reply->error() != QNetworkReply::NoError) {
        QString message = apiErrorMessage(QString::fromUtf8(m_rawBuffer));
        if (message.isEmpty() && statusCode > 0)
            message = tr("Request failed with status %1").arg(statusCode);
        if (message.isEmpty())
            message = tr("Network request failed");
        if (errorCb)
            errorCb(message);
        emit requestFinished();
        return;
    }

    QString result = m_accumulated;
    if (!m_streaming) {
        const QJsonDocument doc = QJsonDocument::fromJson(m_rawBuffer);
        if (!doc.isObject()) {
            if (errorCb)
                errorCb(tr("Failed to parse API response"));
            emit requestFinished();
            return;
        }
        const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) {
            if (errorCb)
                errorCb(tr("API response contains no choices"));
            emit requestFinished();
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
    emit requestFinished();
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
