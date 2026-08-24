#include "AppIcons.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QStyle>

namespace {

QIcon standardIcon(QStyle::StandardPixmap id)
{
    return QApplication::style()->standardIcon(id);
}

QIcon build(const std::function<void(QPainter&, const QRectF&)>& render)
{
    QIcon icon;
    const QColor color = QApplication::palette().color(QPalette::ButtonText);
    for (int size : {16, 24, 32, 48}) {
        for (qreal dpr : {1.0, 2.0}) {
            QPixmap pm(int(size * dpr), int(size * dpr));
            pm.setDevicePixelRatio(dpr);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.setBrush(color);
            render(p, QRectF(0, 0, size, size));
            icon.addPixmap(pm);
        }
    }
    return icon;
}

}

QIcon AppIcons::swapHorizontal()
{
    constexpr qreal kStagger = 2.0;
    return build([kStagger](QPainter& p, const QRectF& r) {
        const qreal u = r.width() / 24;
        const auto point = [&](qreal gx, qreal gy) {
            return QPointF(r.left() + gx * u, r.top() + gy * u);
        };

        QPen pen(p.brush(), r.width() * 0.085);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);

        QPainterPath path;
        path.moveTo(point(4 + kStagger, 7));
        path.lineTo(point(20 + kStagger, 7));
        path.moveTo(point(16 + kStagger, 11));
        path.lineTo(point(20 + kStagger, 7));
        path.lineTo(point(16 + kStagger, 3));

        path.moveTo(point(20 - kStagger, 17));
        path.lineTo(point(4 - kStagger, 17));
        path.moveTo(point(8 - kStagger, 13));
        path.lineTo(point(4 - kStagger, 17));
        path.lineTo(point(8 - kStagger, 21));

        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    });
}

QIcon AppIcons::moveUp()
{
    return standardIcon(QStyle::SP_ArrowUp);
}

QIcon AppIcons::moveDown()
{
    return standardIcon(QStyle::SP_ArrowDown);
}

QIcon AppIcons::reset()
{
    return standardIcon(QStyle::SP_DialogResetButton);
}

QIcon AppIcons::undo()
{
    return standardIcon(QStyle::SP_ArrowBack);
}

QIcon AppIcons::redo()
{
    return standardIcon(QStyle::SP_ArrowForward);
}
