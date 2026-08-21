#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>

class GlobalHotkey : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit GlobalHotkey(QObject* parent = nullptr);
    ~GlobalHotkey() override;

    bool isSupported() const;
    QString lastError() const;
    QString sequence() const;
    bool isActive() const;

    void setSequence(const QString& sequence);
    bool setActive(bool active);

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void activated();

private:
    bool registerOnPlatform();
    void unregisterFromPlatform();

    QString m_sequence = QStringLiteral("Ctrl+Alt+T");
    QString m_error;
    bool m_active = false;

    struct Impl;
    Impl* d = nullptr;
};
