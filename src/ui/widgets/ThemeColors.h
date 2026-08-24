#pragma once

#include <QColor>

class QWidget;

class ThemeColors
{
public:
    static QColor neutralText(const QWidget* widget);
    static QColor successText(const QWidget* widget);
    static QColor errorText(const QWidget* widget);
    static void setTextColor(QWidget* widget, const QColor& color);
};
