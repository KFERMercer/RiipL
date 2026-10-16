#include "ThemeColors.h"

#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>
#include <QWidget>

namespace {

// QStyleHints reports the platform colour scheme, but leaves it Unknown on
// platforms without theme integration, so fall back to palette lightness.
bool isDarkScheme(const QWidget* widget)
{
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown)
        return scheme == Qt::ColorScheme::Dark;
    return widget->palette().color(QPalette::Window).lightness() < 128;
}

}

QColor ThemeColors::neutralText(const QWidget* widget)
{
    return widget->palette().color(QPalette::PlaceholderText);
}

QColor ThemeColors::successText(const QWidget* widget)
{
    return isDarkScheme(widget) ? QColor(0x66, 0xbb, 0x6a) : QColor(0x2e, 0x7d, 0x32);
}

QColor ThemeColors::errorText(const QWidget* widget)
{
    return isDarkScheme(widget) ? QColor(0xef, 0x53, 0x50) : QColor(0xc6, 0x28, 0x28);
}

void ThemeColors::setTextColor(QWidget* widget, const QColor& color)
{
    QPalette palette = widget->palette();
    palette.setColor(widget->foregroundRole(), color);
    widget->setPalette(palette);
}

QString ThemeColors::primaryButtonStyle()
{
    return QStringLiteral(
        "QToolButton { color: white;"
        "              background-color: #007653;"
        "              border: 1px solid transparent;"
        "              border-radius: 0.25em;"
        "              padding: 0.25em; }"
        "QToolButton:hover { background-color: #00805a; }"
        "QToolButton:pressed { background-color: #005c41; }"
        "QToolButton:focus { border: 1px solid palette(highlight); }"
        "QToolButton:disabled { color: rgba(255, 255, 255, 190);"
        "                       background-color: rgba(0, 118, 83, 150); }");
}
