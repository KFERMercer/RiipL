#pragma once

#include <QRect>
#include <QScreen>
#include <QWidget>

namespace GeometryUtils {

inline QSize dialogInitialSize(const QWidget* dialog)
{
    const QRect available = dialog->screen()->availableGeometry();
    return QSize(available.width() / 3, dialog->sizeHint().height());
}

}
