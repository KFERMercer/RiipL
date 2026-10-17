#include "AppIcons.h"

#include <QApplication>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QString>
#include <QStyle>

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

void paintSwapGlyph(QPainter& painter, const QRectF& rect, const QColor& ink)
{
    constexpr qreal kStagger = 2.0;
    const qreal u = rect.width() / 24;
    const auto point = [&](qreal gx, qreal gy) {
        return QPointF(rect.left() + gx * u, rect.top() + gy * u);
    };

    QPen pen(ink, rect.width() * 0.085);
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

    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}

class SwapIconEngine : public QIconEngine
{
public:
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        paintSwapGlyph(*painter, QRectF(rect), ink(mode));
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        // The inherited implementation leaves the pixmap uninitialised.
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return result;
    }

    QIconEngine* clone() const override { return new SwapIconEngine(*this); }

    QString key() const override { return QStringLiteral("RiipLSwapIconEngine"); }

private:
    static QColor ink(QIcon::Mode mode)
    {
        const QPalette::ColorGroup group =
            mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Normal;
        return QApplication::palette().color(group, QPalette::ButtonText);
    }
};

}

QIcon AppIcons::swapHorizontal()
{
    return QIcon(new SwapIconEngine);
}

QIcon AppIcons::moveUp()
{
    return standardIcon(QStyle::SP_ArrowUp);
}

QIcon AppIcons::moveDown()
{
    return standardIcon(QStyle::SP_ArrowDown);
}

QIcon AppIcons::arrowRight()
{
    return standardIcon(QStyle::SP_ArrowRight);
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
