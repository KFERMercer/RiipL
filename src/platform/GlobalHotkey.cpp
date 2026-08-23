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

#ifdef Q_OS_MACOS
#include <HIToolbox/Events.h>
#include <HIToolbox/EventHandler.h>
#include <HIToolbox/HITargets.h>
#endif

struct GlobalHotkey::Impl
{
#ifdef RIIP_HOTKEY_X11
    Display* display = nullptr;
    QSocketNotifier* notifier = nullptr;
    Window root = 0;
    KeyCode keycode = 0;
    unsigned int modifiers = 0;
#elif defined(Q_OS_WIN)
    quint32 key = 0;
    quint32 modifiers = 0;
    bool registered = false;
#elif defined(Q_OS_MACOS)
    EventHotKeyRef hotkeyRef = nullptr;
    EventHandlerRef handlerRef = nullptr;
#endif
};

namespace {

#if defined(RIIP_HOTKEY_X11)
constexpr unsigned int kLockMaskCombinations = LockMask | Mod2Mask | Mod3Mask | Mod5Mask;

bool g_grabFailed = false;

int x11ErrorHandler(Display*, XErrorEvent* event)
{
    if (event->error_code == BadAccess)
        g_grabFailed = true;
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
#elif defined(Q_OS_MACOS)
unsigned int qtToCarbonModifiers(Qt::KeyboardModifiers mods)
{
    unsigned int result = 0;
    if (mods & Qt::ControlModifier)
        result |= controlKey;
    if (mods & Qt::ShiftModifier)
        result |= shiftKey;
    if (mods & Qt::AltModifier)
        result |= optionKey;
    if (mods & Qt::MetaModifier)
        result |= cmdKey;
    return result;
}

int qtKeyToCarbonCode(Qt::Key key)
{
    switch (key) {
    case Qt::Key_A: return kVK_ANSI_A;
    case Qt::Key_B: return kVK_ANSI_B;
    case Qt::Key_C: return kVK_ANSI_C;
    case Qt::Key_D: return kVK_ANSI_D;
    case Qt::Key_E: return kVK_ANSI_E;
    case Qt::Key_F: return kVK_ANSI_F;
    case Qt::Key_G: return kVK_ANSI_G;
    case Qt::Key_H: return kVK_ANSI_H;
    case Qt::Key_I: return kVK_ANSI_I;
    case Qt::Key_J: return kVK_ANSI_J;
    case Qt::Key_K: return kVK_ANSI_K;
    case Qt::Key_L: return kVK_ANSI_L;
    case Qt::Key_M: return kVK_ANSI_M;
    case Qt::Key_N: return kVK_ANSI_N;
    case Qt::Key_O: return kVK_ANSI_O;
    case Qt::Key_P: return kVK_ANSI_P;
    case Qt::Key_Q: return kVK_ANSI_Q;
    case Qt::Key_R: return kVK_ANSI_R;
    case Qt::Key_S: return kVK_ANSI_S;
    case Qt::Key_T: return kVK_ANSI_T;
    case Qt::Key_U: return kVK_ANSI_U;
    case Qt::Key_V: return kVK_ANSI_V;
    case Qt::Key_W: return kVK_ANSI_W;
    case Qt::Key_X: return kVK_ANSI_X;
    case Qt::Key_Y: return kVK_ANSI_Y;
    case Qt::Key_Z: return kVK_ANSI_Z;
    case Qt::Key_0: return kVK_ANSI_0;
    case Qt::Key_1: return kVK_ANSI_1;
    case Qt::Key_2: return kVK_ANSI_2;
    case Qt::Key_3: return kVK_ANSI_3;
    case Qt::Key_4: return kVK_ANSI_4;
    case Qt::Key_5: return kVK_ANSI_5;
    case Qt::Key_6: return kVK_ANSI_6;
    case Qt::Key_7: return kVK_ANSI_7;
    case Qt::Key_8: return kVK_ANSI_8;
    case Qt::Key_9: return kVK_ANSI_9;
    case Qt::Key_F1: return kVK_F1;
    case Qt::Key_F2: return kVK_F2;
    case Qt::Key_F3: return kVK_F3;
    case Qt::Key_F4: return kVK_F4;
    case Qt::Key_F5: return kVK_F5;
    case Qt::Key_F6: return kVK_F6;
    case Qt::Key_F7: return kVK_F7;
    case Qt::Key_F8: return kVK_F8;
    case Qt::Key_F9: return kVK_F9;
    case Qt::Key_F10: return kVK_F10;
    case Qt::Key_F11: return kVK_F11;
    case Qt::Key_F12: return kVK_F12;
    case Qt::Key_Space: return kVK_Space;
    case Qt::Key_Return: return kVK_Return;
    case Qt::Key_Tab: return kVK_Tab;
    case Qt::Key_Escape: return kVK_Escape;
    case Qt::Key_Backspace: return kVK_Delete;
    case Qt::Key_Delete: return kVK_ForwardDelete;
    case Qt::Key_Left: return kVK_LeftArrow;
    case Qt::Key_Right: return kVK_RightArrow;
    case Qt::Key_Down: return kVK_DownArrow;
    case Qt::Key_Up: return kVK_UpArrow;
    case Qt::Key_Home: return kVK_Home;
    case Qt::Key_End: return kVK_End;
    case Qt::Key_PageUp: return kVK_PageUp;
    case Qt::Key_PageDown: return kVK_PageDown;
    case Qt::Key_Minus: return kVK_ANSI_Minus;
    case Qt::Key_Equal: return kVK_ANSI_Equal;
    case Qt::Key_BracketLeft: return kVK_ANSI_LeftBracket;
    case Qt::Key_BracketRight: return kVK_ANSI_RightBracket;
    case Qt::Key_Backslash: return kVK_ANSI_Backslash;
    case Qt::Key_Semicolon: return kVK_ANSI_Semicolon;
    case Qt::Key_Apostrophe: return kVK_ANSI_Quote;
    case Qt::Key_Comma: return kVK_ANSI_Comma;
    case Qt::Key_Period: return kVK_ANSI_Period;
    case Qt::Key_Slash: return kVK_ANSI_Slash;
    default:
        return -1;
    }
}

// Installed per hotkey with the owning GlobalHotkey passed as userData; the
// Carbon event loop dispatches on the main thread, matching Qt's thread.
OSStatus carbonHotkeyEventHandler(EventHandlerCallRef callRef, EventRef event, void* userData)
{
    Q_UNUSED(callRef);
    if (GetEventClass(event) == kEventClassKeyboard && GetEventKind(event) == kEventHotKeyPressed) {
        emit static_cast<GlobalHotkey*>(userData)->activated();
        return noErr;
    }
    return eventNotHandledErr;
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
    g_grabFailed = false;
    for (unsigned int locks = 0; locks <= kLockMaskCombinations; ++locks) {
        const unsigned int combo = locks & kLockMaskCombinations;
        XGrabKey(d->display, code, d->modifiers | combo, d->root, True, GrabModeAsync, GrabModeAsync);
    }
    XSync(d->display, False);
    XSetErrorHandler(previousHandler);
    if (g_grabFailed) {
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
#elif defined(Q_OS_MACOS)
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    Qt::Key key = Qt::Key_unknown;
    if (!splitSequence(m_sequence, &mods, &key)) {
        m_error = tr("Invalid hotkey sequence: %1").arg(m_sequence);
        return false;
    }
    const int keycode = qtKeyToCarbonCode(key);
    if (keycode < 0) {
        m_error = tr("Unsupported key in hotkey sequence: %1").arg(QKeySequence(key).toString(QKeySequence::PortableText));
        return false;
    }
    const UInt32 carbonMods = qtToCarbonModifiers(mods);
    EventHotKeyID hotkeyId;
    hotkeyId.signature = 0x72696970; // 'riip'
    hotkeyId.id = 1;
    const OSStatus status = RegisterEventHotKey(static_cast<UInt32>(keycode), carbonMods, hotkeyId,
                                                GetApplicationEventTarget(), 0, &d->hotkeyRef);
    if (status != noErr) {
        m_error = tr("Global hotkey is already taken by another application");
        return false;
    }
    const EventTypeSpec eventType = { kEventClassKeyboard, kEventHotKeyPressed };
    InstallApplicationEventHandler(carbonHotkeyEventHandler, 1, &eventType, this, &d->handlerRef);
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
#elif defined(Q_OS_MACOS)
    if (d->hotkeyRef) {
        UnregisterEventHotKey(d->hotkeyRef);
        d->hotkeyRef = nullptr;
    }
    if (d->handlerRef) {
        RemoveEventHandler(d->handlerRef);
        d->handlerRef = nullptr;
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
