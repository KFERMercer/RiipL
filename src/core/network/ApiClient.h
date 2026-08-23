#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

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
    bool m_idleTimedOut = false;
    DoneCallback m_onDone;
    DeltaCallback m_onDelta;
    ErrorCallback m_onError;
};
