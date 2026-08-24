#include "ThemeColors.h"

#include <QPalette>
#include <QWidget>

namespace {

bool isDarkScheme(const QPalette& palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

} // namespace

QColor ThemeColors::neutralText(const QWidget* widget)
{
    return widget->palette().color(QPalette::PlaceholderText);
}

// QPalette has no success/error roles, so semantic hues are defined here and
// selected by scheme darkness to stay legible on both light and dark themes.
QColor ThemeColors::successText(const QWidget* widget)
{
    return isDarkScheme(widget->palette()) ? QColor(0x66, 0xbb, 0x6a) : QColor(0x2e, 0x7d, 0x32);
}

QColor ThemeColors::errorText(const QWidget* widget)
{
    return isDarkScheme(widget->palette()) ? QColor(0xef, 0x53, 0x50) : QColor(0xc6, 0x28, 0x28);
}

void ThemeColors::setTextColor(QWidget* widget, const QColor& color)
{
    QPalette palette = widget->palette();
    palette.setColor(widget->foregroundRole(), color);
    widget->setPalette(palette);
}
