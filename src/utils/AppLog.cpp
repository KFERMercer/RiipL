#include "AppLog.h"

#include <QCoreApplication>

QDebug AppLog::warning()
{
    return qWarning() << qPrintable(QCoreApplication::applicationName() + QLatin1Char(':'));
}
