#include "AppIcons.h"

#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QString>
#include <QStyle>
#include <functional>

namespace {

QIcon standardIcon(QStyle::StandardPixmap id)
{
    return QApplication::style()->standardIcon(id);
}

QString trayPath(TrayIcons::Family family, TranslationActivity::State state)
{
    const QString familyName = family == TrayIcons::Family::Dude
        ? QStringLiteral("dude")
        : QStringLiteral("app");
    switch (state) {
    case TranslationActivity::State::Ready:
        return QStringLiteral(":/icons/tray/%1/ready.svg").arg(familyName);
    case TranslationActivity::State::Busy:
        return QStringLiteral(":/icons/tray/%1/busy.svg").arg(familyName);
    case TranslationActivity::State::Done:
        return QStringLiteral(":/icons/tray/%1/done.svg").arg(familyName);
    case TranslationActivity::State::Failure:
        return QStringLiteral(":/icons/tray/%1/failure.svg").arg(familyName);
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString familyMarkPath(TrayIcons::Family family)
{
    return QStringLiteral(":/icons/%1.svg").arg(
        family == TrayIcons::Family::Dude ? QStringLiteral("dude") : QStringLiteral("app"));
}

// A QIcon keeps the pixmaps it rendered, so one instance per path is kept.
QIcon cachedTrayIcon(const QString& path)
{
    static QHash<QString, QIcon> icons;
    const auto cached = icons.constFind(path);
    if (cached != icons.constEnd())
        return *cached;
    return *icons.insert(path, QIcon(path));
}

QIcon build(const std::function<void(QPainter&, const QRectF&)>& render)
{
    // Rendered once at high resolution; QIcon scales the result down for every
    // requested logical size and device pixel ratio.
    constexpr int kSourceSize = 96;
    QPixmap pixmap(kSourceSize, kSourceSize);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QApplication::palette().color(QPalette::ButtonText));
    render(painter, QRectF(0, 0, kSourceSize, kSourceSize));
    painter.end();
    return QIcon(pixmap);
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

QIcon AppIcons::tray(TrayIcons::Family family, TranslationActivity::State state)
{
    return cachedTrayIcon(trayPath(family, state));
}

QIcon AppIcons::trayIdle(TrayIcons::Family family)
{
    return cachedTrayIcon(familyMarkPath(family));
}
