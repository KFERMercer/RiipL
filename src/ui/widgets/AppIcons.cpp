#include "AppIcons.h"

#include <cmath>

#include <QApplication>
#include <QPalette>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

namespace {

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

void drawArrowHead(QPainter& p, const QPointF& tip, const QPointF& direction, qreal size)
{
    const QPointF dir = direction / std::hypot(direction.x(), direction.y());
    const QPointF normal(-dir.y(), dir.x());
    const qreal base = size * 0.9;
    QPolygonF head;
    head << tip
         << tip - dir * size + normal * base * 0.5
         << tip - dir * size - normal * base * 0.5;
    p.drawPolygon(head);
}

void renderArcArrow(QPainter& p, const QRectF& r)
{
    const qreal margin = r.width() * 0.14;
    const QRectF circle = r.adjusted(margin, margin, -margin, -margin);
    const QPointF center = circle.center();
    const qreal radius = circle.width() / 2;

    QPen pen(p.brush(), r.width() * 0.09);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const int startAngle = 40 * 16;
    const int spanAngle = 290 * 16;
    p.drawArc(circle, startAngle, spanAngle);

    p.setBrush(p.pen().brush());
    const qreal theta = qDegreesToRadians(40.0);
    const QPointF tip(center.x() + radius * std::cos(theta),
                      center.y() - radius * std::sin(theta));
    const QPointF tangent(std::sin(theta), std::cos(theta));
    drawArrowHead(p, tip, tangent, r.width() * 0.20);
}

}

QIcon AppIcons::swapHorizontal()
{
    return build([](QPainter& p, const QRectF& r) {
        const qreal head = r.width() * 0.18;
        const qreal y1 = r.top() + r.height() * 0.30;
        const qreal y2 = r.top() + r.height() * 0.70;
        const qreal xL = r.left() + r.width() * 0.12;
        const qreal xR = r.right() - r.width() * 0.12;

        QPen pen(p.brush(), r.width() * 0.085);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawLine(QPointF(xL, y1), QPointF(xR - head * 0.6, y1));
        p.drawLine(QPointF(xR, y2), QPointF(xL + head * 0.6, y2));

        p.setPen(Qt::NoPen);
        drawArrowHead(p, QPointF(xR, y1), QPointF(1, 0), head);
        drawArrowHead(p, QPointF(xL, y2), QPointF(-1, 0), head);
    });
}

QIcon AppIcons::reset()
{
    return build([](QPainter& p, const QRectF& r) {
        renderArcArrow(p, r);
    });
}

QIcon AppIcons::undo()
{
    return build([](QPainter& p, const QRectF& r) {
        p.save();
        p.translate(r.center().x(), 0);
        p.scale(-1, 1);
        p.translate(-r.center().x(), 0);
        renderArcArrow(p, r);
        p.restore();
    });
}
