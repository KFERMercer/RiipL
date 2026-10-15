#pragma once

#include <QObject>
#include <QString>

// The clipboard as this session lets the application reach it. A Wayland client is
// offered the selection only while it holds the keyboard focus, so on such a session
// KDE's Klipper service answers for the clipboard whenever the window is not focused:
// it reports every change and is read and written in place of QClipboard, which an
// unfocused client only ever sees its own last text through. Every other session
// leaves the clipboard to QClipboard. Klipper answers with the newest text of its
// history, so a clipboard holding what it does not keep — an image, a password —
// reads as the text that came before it.
class SessionClipboard : public QObject
{
    Q_OBJECT

public:
    explicit SessionClipboard(QObject* parent = nullptr);

    // Trimmed; empty when the clipboard holds no text.
    QString text() const;
    void setText(const QString& text);

signals:
    // The clipboard may hold different text than it did.
    void changed();

private:
    void attach();
    void detach();
    bool serviceCarriesClipboard() const;

    bool m_serviceAttached = false;
};
