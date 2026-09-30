#pragma once

#include <QByteArray>
#include <QHttpHeaders>
#include <QJsonObject>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

class ApiClient : public QObject
{
    Q_OBJECT

public:
    // Names every failure the network and translation layers surface. The layers
    // report the code instead of rendering it, so the text can follow the
    // language in force when it is shown rather than when the request failed.
    enum class ErrorCode {
        BaseUrlMissing,
        Cancelled,
        TimedOut,
        // detail: the Qt error string.
        NetworkFailure,
        // detail: the HTTP status code.
        HttpStatus,
        // detail: the message the API supplied, which is already in its own words.
        ServerMessage,
        InvalidResponse,
        NoChoices,
        NothingToTranslate,
        NothingToLookUp,
        // detail: the line count the answer was expected to hold.
        LineCountMismatch
    };

    struct Error
    {
        ErrorCode code = ErrorCode::NetworkFailure;
        QString detail;

        // A receiver that keeps the Error can render it again after a change.
        QString text() const;
    };

    using DoneCallback = std::function<void(const QString&)>;
    using DeltaCallback = std::function<void(const QString&)>;
    using ErrorCallback = std::function<void(const Error&)>;

    explicit ApiClient(QObject* parent = nullptr);

    void sendChatRequest(const QJsonObject& body,
                         DoneCallback onDone,
                         DeltaCallback onStream,
                         ErrorCallback onError);
    void cancel();
    bool busy() const { return m_reply != nullptr; }

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
    void consumeStreamBuffer();
    QString apiErrorMessage(const QString& body) const;

    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply* m_reply = nullptr;
    QByteArray m_streamBuffer;
    QByteArray m_rawBuffer;
    QString m_accumulated;
    bool m_streaming = false;
    bool m_doneSent = false;
    bool m_userCancelled = false;
    DoneCallback m_onDone;
    DeltaCallback m_onDelta;
    ErrorCallback m_onError;
};

Q_DECLARE_METATYPE(ApiClient::Error)
