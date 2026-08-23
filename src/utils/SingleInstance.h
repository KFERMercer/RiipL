#pragma once

#include <QObject>
#include <QLockFile>
#include <QString>

class QLocalServer;

class SingleInstance : public QObject
{
    Q_OBJECT

public:
    // Startup arbitration outcome. Primary owns both the lock file and the
    // activation listener, Secondary defers to the already running instance,
    // and Error means the lock was won but the listener could not be created.
    enum class Role { Primary, Secondary, Error };

    explicit SingleInstance(QObject* parent = nullptr);

    Role tryLock();
    bool isPrimary() const { return m_server != nullptr; }
    void notifyExistingInstance();

signals:
    void activationRequested();

private:
    static QString instanceKey();
    static QString lockFilePath();

    QLockFile m_lock;
    QLocalServer* m_server = nullptr;
};
