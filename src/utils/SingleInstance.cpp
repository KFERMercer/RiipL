#include "SingleInstance.h"

#include <QCryptographicHash>
#include <QLocalServer>
#include <QLocalSocket>

namespace {
constexpr int kConnectTimeoutMs = 300;
}

QString SingleInstance::instanceKey()
{
    const QByteArray seed = qgetenv("USERNAME") + qgetenv("USER") + qgetenv("LOGNAME");
    const QByteArray hash = QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex();
    return QStringLiteral("riipl-") + QString::fromLatin1(hash.left(16));
}

SingleInstance::SingleInstance(QObject* parent)
    : QObject(parent)
{
}

bool SingleInstance::tryLock()
{
    QLocalSocket probe;
    probe.connectToServer(instanceKey());
    if (probe.waitForConnected(kConnectTimeoutMs)) {
        probe.abort();
        return false;
    }

    QLocalServer::removeServer(instanceKey());

    m_server = new QLocalServer(this);
    if (!m_server->listen(instanceKey())) {
        m_server->deleteLater();
        m_server = nullptr;
        return true;
    }

    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        QLocalSocket* connection = m_server->nextPendingConnection();
        if (!connection)
            return;
        connect(connection, &QLocalSocket::disconnected, connection, &QLocalSocket::deleteLater);
        emit activationRequested();
    });
    return true;
}

void SingleInstance::notifyExistingInstance()
{
    QLocalSocket socket;
    socket.connectToServer(instanceKey());
    if (socket.waitForConnected(kConnectTimeoutMs)) {
        socket.write("activate\n");
        socket.flush();
        socket.waitForBytesWritten(kConnectTimeoutMs);
    }
    socket.disconnectFromServer();
}
