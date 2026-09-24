#pragma once

#include <QByteArray>
#include <QHttpHeaders>
#include <QJsonObject>
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
    using DoneCallback = std::function<void(const QString&)>;
    using DeltaCallback = std::function<void(const QString&)>;
    using ErrorCallback = std::function<void(const QString&)>;

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
