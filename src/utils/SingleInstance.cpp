#include "SingleInstance.h"

#include <QCryptographicHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>

namespace {
constexpr int kConnectTimeoutMs = 300;

QString runtimeDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
}
}

QString SingleInstance::instanceKey()
{
    const QByteArray seed = qgetenv("USERNAME") + qgetenv("USER") + qgetenv("LOGNAME");
    const QByteArray hash = QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex();
    return QStringLiteral("riipl-") + QString::fromLatin1(hash.left(16));
}

QString SingleInstance::lockFilePath()
{
    return runtimeDirectory() + QStringLiteral("/%1.lock").arg(instanceKey());
}

QString SingleInstance::serverName()
{
#ifdef Q_OS_UNIX
    return runtimeDirectory() + QLatin1Char('/') + instanceKey();
#else
    return instanceKey();
#endif
}

SingleInstance::SingleInstance(QObject* parent)
    : QObject(parent)
    , m_lock(lockFilePath())
{
}

SingleInstance::Role SingleInstance::tryLock()
{
    if (runtimeDirectory().isEmpty())
        return Role::Error;

    // The file lock arbitrates ownership atomically, so concurrent cold starts
    // cannot both reach the listener setup below.
    if (!m_lock.tryLock(0))
        return Role::Secondary;

    // Only the lock owner touches the server name, so removing a socket file
    // left behind by a crashed instance never disturbs a live listener.
    QLocalServer::removeServer(serverName());

    m_server = new QLocalServer(this);
    if (!m_server->listen(serverName())) {
        delete m_server;
        m_server = nullptr;
        m_lock.unlock();
        return Role::Error;
    }

    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        QLocalSocket* connection = m_server->nextPendingConnection();
        if (!connection)
            return;
        connect(connection, &QLocalSocket::disconnected, connection, &QLocalSocket::deleteLater);
        emit activationRequested();
    });
    return Role::Primary;
}

bool SingleInstance::notifyExistingInstance()
{
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(kConnectTimeoutMs))
        return false;
    socket.disconnectFromServer();
    return true;
}
