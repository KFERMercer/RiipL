#include <QtTest>

#include "utils/AppLog.h"

#include <QCoreApplication>

namespace {

QString capturedMessage;

void captureMessage(QtMsgType, const QMessageLogContext&, const QString& message)
{
    capturedMessage = message;
}

}

class TestAppLog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void prefixesTheApplicationName();
    void followsTheApplicationName();
};

void TestAppLog::initTestCase()
{
    qInstallMessageHandler(captureMessage);
}

void TestAppLog::prefixesTheApplicationName()
{
    QCoreApplication::setApplicationName(QStringLiteral("RiipL"));
    AppLog::warning() << "cannot write config file" << QStringLiteral("/tmp/config.json");
    QCOMPARE(capturedMessage,
             QStringLiteral("RiipL: cannot write config file \"/tmp/config.json\""));
}

void TestAppLog::followsTheApplicationName()
{
    QCoreApplication::setApplicationName(QStringLiteral("Renamed"));
    AppLog::warning() << "cannot read the clipboard";
    QCOMPARE(capturedMessage, QStringLiteral("Renamed: cannot read the clipboard"));
}

QTEST_MAIN(TestAppLog)
#include "tst_applog.moc"
