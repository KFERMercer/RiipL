#pragma once

#include <QByteArray>
#include <QHttpHeaders>
#include <QJsonObject>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace ApiTimeout {

// Shortest window a request may run with; the settings page offers the same floor.
inline constexpr int minimumMs = 1000;

}

class ApiClient : public QObject
{
    Q_OBJECT

public:
    // Names every failure the network and translation layers surface. The layers
    // report the code instead of rendering it, so the text can follow the
    // language in force when it is shown rather than when the request failed.
    enum class ErrorCode {
        BaseUrlMissing,
        BaseUrlInvalid,
        TimedOut,
        // detail: the Qt error string.
        NetworkFailure,
        // detail: the HTTP status code.
        HttpStatus,
        // detail: the message the API supplied, which is already in its own words.
        ServerMessage,
        InvalidResponse,
        NoChoices,
        // The response passed the size cap and was dropped.
        ResponseTooLarge,
        NothingToTranslate,
        NothingToLookUp,
    };

    struct Error
    {
        ErrorCode code = ErrorCode::NetworkFailure;
        QString detail;

        // A receiver that keeps the Error can render it again after a change.
        QString text() const;
    };

    // Settings one request runs with, so a caller can pass values it has not
    // committed yet.
    struct Connection
    {
        QString baseUrl;
        QString apiKey;
        QString customHeaders;
        int timeoutMs = ApiTimeout::minimumMs;

        static Connection configured();
        static Connection fromValues(const QJsonObject& values);
    };

    using DoneCallback = std::function<void(const QString&)>;
    using DeltaCallback = std::function<void(const QString&)>;
    using ErrorCallback = std::function<void(const Error&)>;
    using ModelsCallback = std::function<void(const QStringList&)>;

    explicit ApiClient(QObject* parent = nullptr);

    void sendChatRequest(const QJsonObject& body,
                         DoneCallback onDone,
                         DeltaCallback onStream,
                         ErrorCallback onError);
    void cancel();
    bool isBusy() const { return m_reply != nullptr; }

    // Reads the ids the endpoint's model list reports; a body without a model
    // array fails as ErrorCode::InvalidResponse.
    void listModels(const Connection& connection, ModelsCallback onDone, ErrorCallback onError);

    // Model ids of a /models response body, in the order it lists them; nothing
    // when the body holds no model array.
    static std::optional<QStringList> parseModelIds(const QByteArray& body);

    // Parses user-configured header lines of the form "Name: value";
    // malformed lines are ignored.
    static QHttpHeaders parseCustomHeaders(const QString& raw);

    // Base URL with redundant trailing path slashes removed.
    static QUrl normalizedBaseUrl(const QString& baseUrl);

signals:
    void requestFinished();

private:
    void onReadyRead();
    void onFinished();
    // \p flush reads a trailing frame the stream ended without a newline after.
    void consumeStreamBuffer(bool flush);
    void cancelModels();
    void onModelsReadyRead();
    void onModelsFinished();

    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply* m_reply = nullptr;
    QByteArray m_streamBuffer;
    QByteArray m_rawBuffer;
    QString m_accumulated;
    qsizetype m_receivedBytes = 0;
    bool m_streaming = false;
    bool m_doneSent = false;
    bool m_overflowed = false;
    DoneCallback m_onDone;
    DeltaCallback m_onDelta;
    ErrorCallback m_onError;
    QNetworkReply* m_modelsReply = nullptr;
    QByteArray m_modelsBuffer;
    bool m_modelsOverflowed = false;
    ModelsCallback m_modelsOnDone;
    ErrorCallback m_modelsOnError;
};

Q_DECLARE_METATYPE(ApiClient::Error)
