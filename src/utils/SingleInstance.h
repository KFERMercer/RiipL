#pragma once

#include <QObject>
#include <QString>

class QLocalServer;

class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(QObject* parent = nullptr);

    bool tryLock();
    bool isPrimary() const { return m_server != nullptr; }
    void notifyExistingInstance();

signals:
    void activationRequested();

private:
    static QString instanceKey();

    QLocalServer* m_server = nullptr;
};
