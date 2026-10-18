#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QHttpHeaders>
#include <QJsonObject>
#include <QList>
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

// Carries one request at a time to an OpenAI-compatible API.
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
        // The body lacks a model or a message the specification requires.
        InvalidRequest,
        TimedOut,
        // detail: the Qt error string.
        NetworkFailure,
        // detail: the HTTP status code.
        HttpStatus,
        // detail: the message the API supplied, which is already in its own words.
        ServerMessage,
        InvalidResponse,
        NoChoices,
        // The answer carried no text.
        NoContent,
        // The model reached the token limit before finishing its answer.
        Truncated,
        // The provider's content filter stopped the model.
        Filtered,
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

    struct Model
    {
        QString id;
        // Display label some endpoints send as name, display_name or displayName.
        QString name;
    };

    using DoneCallback = std::function<void(const QString&)>;
    using DeltaCallback = std::function<void(const QString&)>;
    using ErrorCallback = std::function<void(const Error&)>;
    using ModelsCallback = std::function<void(const QList<Model>&)>;

    explicit ApiClient(QObject* parent = nullptr);

    // Sends a CreateChatCompletionRequest body; a streamed body delivers its text
    // through \p onStream. Starting a request drops the one in flight.
    void sendChatRequest(const QJsonObject& body,
                         DoneCallback onDone,
                         DeltaCallback onStream,
                         ErrorCallback onError);
    // Drops the request in flight, reporting the client idle without invoking
    // the dropped request's callbacks.
    void cancel();

    // Lists the models the connection's endpoint offers.
    void listModels(const Connection& connection, ModelsCallback onDone, ErrorCallback onError);

    static std::optional<QList<Model>> parseModels(const QByteArray& body);

    static QHttpHeaders parseCustomHeaders(const QString& raw);

    static QJsonObject withExtraBody(const QJsonObject& body);

    static QUrl normalizedBaseUrl(const QString& baseUrl);

signals:
    // The client carries no request, whether it settled or was cancelled.
    void requestFinished();

private:
    enum class RequestKind {
        None,
        Chat,
        Stream,
        Models,
    };

    void onReadyRead();
    void onFinished();
    // \p flush reads a trailing frame the stream ended without a newline after.
    void consumeStreamBuffer(bool flush);
    void consumeStreamLine(QByteArrayView line);
    void dispatchStreamEvent();
    void processStreamPayload(const QByteArray& payload);
    void deliverStreamDeltas();
    void resetRequestState(RequestKind kind);
    // Ends the request in flight without reporting it, so a start path can
    // replace it before observers see the client idle.
    void abortRequest();
    void clearCallbacks();

    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply* m_reply = nullptr;
    RequestKind m_kind = RequestKind::None;
    QByteArray m_body;
    qsizetype m_receivedBytes = 0;
    bool m_overflowed = false;
    // Stream state: bytes not yet split into lines, the data of the event being
    // assembled, the pieces not yet delivered, and whether the terminator arrived.
    QByteArray m_streamBuffer;
    QByteArray m_streamEventData;
    QStringList m_pendingDeltas;
    bool m_doneSent = false;
    std::optional<Error> m_streamError = std::nullopt;
    QString m_accumulated;
    QString m_refusal;
    QString m_finishReason;

    DoneCallback m_onDone;
    DeltaCallback m_onDelta;
    ErrorCallback m_onError;
    ModelsCallback m_onModels;
};

Q_DECLARE_METATYPE(ApiClient::Error)
