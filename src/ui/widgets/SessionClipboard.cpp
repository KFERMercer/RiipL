#include "SessionClipboard.h"

#include <QClipboard>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDebug>
#include <QGuiApplication>
#include <QRegularExpression>

namespace {

constexpr char kService[] = "org.kde.klipper";
constexpr char kPath[] = "/klipper";
constexpr char kInterface[] = "org.kde.klipper.klipper";
// Klipper answers from memory, so a call that has not come back within a second names
// a service that is not answering, and bounds how long a busy one freezes the window.
constexpr int kCallTimeoutMs = 1000;

bool sessionUsesWayland()
{
    return qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland")
        || QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

// Klipper names a history entry it cannot render as text by the size of the image it
// holds, e.g. "▨ 1920x1080" (historymodel.cpp). The prefix is hard-coded while the
// separator between the two numbers is translated, so both are allowed for.
bool isImagePlaceholder(const QString& text)
{
    static const QRegularExpression pattern(
        QStringLiteral("^\\x{25A8} \\d+\\s*[x\u00D7X]\\s*\\d+$"));
    return pattern.match(text).hasMatch();
}

QDBusMessage methodCall(const QString& method)
{
    return QDBusMessage::createMethodCall(QString::fromLatin1(kService),
                                          QString::fromLatin1(kPath),
                                          QString::fromLatin1(kInterface), method);
}

}

SessionClipboard::SessionClipboard(QObject* parent)
    : QObject(parent)
{
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this,
            &SessionClipboard::changed);

    if (!sessionUsesWayland())
        return;

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusConnectionInterface* busInterface = bus.interface();
    if (!busInterface)
        return;

    // Klipper registers its service while the Plasma clipboard applet runs, which can
    // be after this application has started, so the route follows the owner.
    auto* watcher = new QDBusServiceWatcher(QString::fromLatin1(kService), bus,
                                            QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &SessionClipboard::attach);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, &SessionClipboard::detach);

    busInterface->setTimeout(kCallTimeoutMs);
    const QDBusReply<bool> registered =
        busInterface->isServiceRegistered(QString::fromLatin1(kService));
    if (registered.isValid() && registered.value())
        attach();
}

// A route whose change report never arrives cannot follow the clipboard.
void SessionClipboard::attach()
{
    if (m_serviceAttached)
        return;
    if (!QDBusConnection::sessionBus().connect(QString::fromLatin1(kService),
                                               QString::fromLatin1(kPath),
                                               QString::fromLatin1(kInterface),
                                               QStringLiteral("clipboardHistoryUpdated"), this,
                                               SLOT(changed()))) {
        return;
    }
    m_serviceAttached = true;
    // The service saw the clipboard before this route existed, so what it holds now
    // has not been reported yet.
    emit changed();
}

void SessionClipboard::detach()
{
    if (!m_serviceAttached)
        return;
    QDBusConnection::sessionBus().disconnect(QString::fromLatin1(kService),
                                             QString::fromLatin1(kPath),
                                             QString::fromLatin1(kInterface),
                                             QStringLiteral("clipboardHistoryUpdated"), this,
                                             SLOT(changed()));
    m_serviceAttached = false;
}

bool SessionClipboard::serviceCarriesClipboard() const
{
    return m_serviceAttached && !QGuiApplication::focusWindow();
}

QString SessionClipboard::text() const
{
    if (!serviceCarriesClipboard())
        return QGuiApplication::clipboard()->text().trimmed();

    const QDBusMessage answer = QDBusConnection::sessionBus().call(
        methodCall(QStringLiteral("getClipboardContents")), QDBus::Block, kCallTimeoutMs);
    if (answer.type() != QDBusMessage::ReplyMessage) {
        qWarning() << "RiipL: cannot read the clipboard through Klipper" << answer.errorMessage();
        return QString();
    }
    if (answer.arguments().isEmpty())
        return QString();
    const QString text = answer.arguments().constFirst().toString();
    return isImagePlaceholder(text) ? QString() : text.trimmed();
}

void SessionClipboard::setText(const QString& text)
{
    if (!serviceCarriesClipboard()) {
        QGuiApplication::clipboard()->setText(text);
        return;
    }
    // Klipper drops an empty text itself, so there is nothing to send.
    if (text.isEmpty())
        return;
    const QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(
        methodCall(QStringLiteral("setClipboardContents")) << text);
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [](QDBusPendingCallWatcher* watcher) {
                if (watcher->isError())
                    qWarning() << "RiipL: cannot write the clipboard through Klipper"
                               << watcher->error().message();
                watcher->deleteLater();
            });
}
