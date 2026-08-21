#include "GlobalHotkey.h"

#include <QKeySequence>
#include <memory>

#ifdef Q_OS_WIN
#include <QGuiApplication>
#endif

#if defined(RIIP_HAVE_X11)
#define RIIP_HOTKEY_X11 1
#include <X11/Xlib.h>
#include <QSocketNotifier>
#endif

struct GlobalHotkey::Impl
{
#ifdef RIIP_HOTKEY_X11
    Display* display = nullptr;
    QSocketNotifier* notifier = nullptr;
    Window root = 0;
    KeyCode keycode = 0;
    unsigned int modifiers = 0;
    bool grabFailed = false;
#elif defined(Q_OS_WIN)
    quint32 key = 0;
    quint32 modifiers = 0;
    bool registered = false;
#endif
};

namespace {

#if defined(RIIP_HOTKEY_X11)
constexpr unsigned int kLockMaskCombinations = LockMask | Mod2Mask | Mod3Mask | Mod5Mask;

int x11ErrorHandler(Display*, XErrorEvent*)
{
    return 0;
}

unsigned int qtToX11Modifiers(Qt::KeyboardModifiers mods)
{
    unsigned int result = 0;
    if (mods & Qt::ControlModifier)
        result |= ControlMask;
    if (mods & Qt::ShiftModifier)
        result |= ShiftMask;
    if (mods & Qt::AltModifier)
        result |= Mod1Mask;
    if (mods & Qt::MetaModifier)
        result |= Mod4Mask;
    return result;
}
#endif

bool splitSequence(const QString& sequence, Qt::KeyboardModifiers* mods, Qt::Key* key)
{
    const QKeySequence parsed(sequence, QKeySequence::PortableText);
    if (parsed.count() != 1 || parsed[0].key() == Qt::Key_unknown)
        return false;
    *key = parsed[0].key();
    *mods = static_cast<Qt::KeyboardModifiers>(parsed[0].keyboardModifiers());
    const Qt::KeyboardModifiers meaningful = Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    if ((*mods & meaningful) == Qt::NoModifier)
        return false;
    return true;
}

}

GlobalHotkey::GlobalHotkey(QObject* parent)
    : QObject(parent)
    , d(new Impl)
{
#ifdef Q_OS_WIN
    qApp->installNativeEventFilter(this);
#endif
}

GlobalHotkey::~GlobalHotkey()
{
    setActive(false);
#ifdef Q_OS_WIN
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
#endif
    delete d;
}

bool GlobalHotkey::isSupported() const
{
#if defined(RIIP_HOTKEY_X11)
    return true;
#elif defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return true;
#else
    return false;
#endif
}

QString GlobalHotkey::lastError() const
{
    return m_error;
}

QString GlobalHotkey::sequence() const
{
    return m_sequence;
}

bool GlobalHotkey::isActive() const
{
    return m_active;
}

void GlobalHotkey::setSequence(const QString& sequence)
{
    if (m_sequence == sequence)
        return;
    const bool wasActive = m_active;
    if (wasActive)
        setActive(false);
    m_sequence = sequence;
    if (wasActive)
        setActive(true);
}

bool GlobalHotkey::registerOnPlatform()
{
    m_error.clear();
#if defined(RIIP_HOTKEY_X11)
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    Qt::Key key = Qt::Key_unknown;
    if (!splitSequence(m_sequence, &mods, &key)) {
        m_error = tr("Invalid hotkey sequence: %1").arg(m_sequence);
        return false;
    }
    if (!d->display) {
        d->display = XOpenDisplay(nullptr);
        if (!d->display) {
            m_error = tr("X11 display is not available. Global hotkeys require an X11 session.");
            return false;
        }
    }
    const QString keyText = QKeySequence(key).toString(QKeySequence::PortableText);
    const KeySym symbol = XStringToKeysym(keyText.toLower().toUtf8().constData());
    if (symbol == NoSymbol) {
        m_error = tr("Unsupported key in hotkey sequence: %1").arg(keyText);
        return false;
    }
    const KeyCode code = XKeysymToKeycode(d->display, symbol);
    if (code == 0) {
        m_error = tr("Key is not available on this keyboard: %1").arg(keyText);
        return false;
    }
    d->keycode = code;
    d->modifiers = qtToX11Modifiers(mods);
    d->root = DefaultRootWindow(d->display);

    XErrorHandler previousHandler = XSetErrorHandler(x11ErrorHandler);
    d->grabFailed = false;
    for (unsigned int locks = 0; locks <= kLockMaskCombinations; ++locks) {
        const unsigned int combo = locks & kLockMaskCombinations;
        XGrabKey(d->display, code, d->modifiers | combo, d->root, True, GrabModeAsync, GrabModeAsync);
    }
    XSync(d->display, False);
    XSetErrorHandler(previousHandler);
    if (d->grabFailed) {
        m_error = tr("Global hotkey is already taken by another application");
        for (unsigned int locks = 0; locks <= kLockMaskCombinations; ++locks) {
            const unsigned int combo = locks & kLockMaskCombinations;
            XUngrabKey(d->display, code, d->modifiers | combo, d->root);
        }
        return false;
    }

    delete d->notifier;
    d->notifier = new QSocketNotifier(ConnectionNumber(d->display), QSocketNotifier::Read, this);
    connect(d->notifier, &QSocketNotifier::activated, this, [this]() {
        while (d->display && XPending(d->display) > 0) {
            XEvent event;
            XNextEvent(d->display, &event);
            if (event.type != KeyPress)
                continue;
            const XKeyEvent& press = event.xkey;
            const unsigned int cleanState = press.state & ~kLockMaskCombinations;
            if (press.keycode == d->keycode && cleanState == d->modifiers)
                emit activated();
        }
    });
    return true;
#elif defined(Q_OS_WIN)
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    Qt::Key key = Qt::Key_unknown;
    if (!splitSequence(m_sequence, &mods, &key)) {
        m_error = tr("Invalid hotkey sequence: %1").arg(m_sequence);
        return false;
    }
    quint32 winMods = MOD_NOREPEAT;
    if (mods & Qt::ControlModifier)
        winMods |= MOD_CONTROL;
    if (mods & Qt::AltModifier)
        winMods |= MOD_ALT;
    if (mods & Qt::ShiftModifier)
        winMods |= MOD_SHIFT;
    if (mods & Qt::MetaModifier)
        winMods |= MOD_WIN;
    const int vk = static_cast<int>(key);
    d->key = static_cast<quint32>(vk);
    d->modifiers = winMods;
    d->registered = RegisterHotKey(nullptr, 0xF11E, winMods, vk);
    if (!d->registered) {
        m_error = tr("Global hotkey is already taken by another application");
        return false;
    }
    return true;
#else
    m_error = tr("Global hotkeys are not supported on this platform");
    return false;
#endif
}

void GlobalHotkey::unregisterFromPlatform()
{
#if defined(RIIP_HOTKEY_X11)
    delete d->notifier;
    d->notifier = nullptr;
    if (d->display && d->keycode) {
        for (unsigned int locks = 0; locks <= kLockMaskCombinations; ++locks) {
            const unsigned int combo = locks & kLockMaskCombinations;
            XUngrabKey(d->display, d->keycode, d->modifiers | combo, d->root);
        }
        XSync(d->display, False);
    }
    d->keycode = 0;
    if (d->display) {
        XCloseDisplay(d->display);
        d->display = nullptr;
    }
#elif defined(Q_OS_WIN)
    if (d->registered) {
        UnregisterHotKey(nullptr, 0xF11E);
        d->registered = false;
    }
#endif
}

bool GlobalHotkey::setActive(bool active)
{
    if (active == m_active)
        return m_active || m_error.isEmpty();
    if (active) {
        if (!registerOnPlatform())
            return false;
        m_active = true;
    } else {
        unregisterFromPlatform();
        m_active = false;
    }
    return true;
}

bool GlobalHotkey::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*)
{
#ifdef Q_OS_WIN
    if (eventType == QByteArrayLiteral("windows_generic_MSG") && message && d->registered) {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_HOTKEY && static_cast<quint32>(msg->wParam) == 0xF11E) {
            emit activated();
            return true;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
#endif
    return false;
}
