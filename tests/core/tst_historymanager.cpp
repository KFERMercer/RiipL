#include <QtTest>

#include "TestSupport.h"
#include "core/history/HistoryManager.h"

#include <QFile>

namespace {

TranslationRecord record(int index)
{
    TranslationRecord record;
    record.timestamp = index + 1;
    record.source = QStringLiteral("s%1").arg(index);
    record.target = QStringLiteral("t%1").arg(index);
    record.sourceLang = QStringLiteral("auto");
    record.targetLang = QStringLiteral("en");
    return record;
}

QStringList fileLines(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QStringList lines;
    for (const QByteArray& line : file.readAll().split('\n')) {
        if (!line.trimmed().isEmpty())
            lines << QString::fromUtf8(line);
    }
    return lines;
}

QString historyPath()
{
    return TestSupport::tempDir() + QStringLiteral("/history.json");
}

}

class TestHistoryManager : public QObject
{
    Q_OBJECT

private slots:
    void trimsToMaxRecords();
    void announcesRecordsDroppedByALowerLimit();
    void appendsRecordsAsTheyArrive();
    void compactsOnceTheFileOutgrowsTheCap();
    void compactsOnTheDebounce();
    void skipsATruncatedLine();
};

void TestHistoryManager::trimsToMaxRecords()
{
    QDir().mkpath(TestSupport::tempDir());
    const QString path = historyPath();

    {
        HistoryManager manager(path);
        manager.setMaxRecords(3);
        for (int i = 0; i < 5; ++i)
            manager.addRecord(record(i));
        QCOMPARE(manager.records().size(), 3);
        QCOMPARE(manager.records().first().source, QStringLiteral("s4"));
        QCOMPARE(manager.records().last().source, QStringLiteral("s2"));
    }

    // The dropped records leave no lines behind.
    QCOMPARE(fileLines(path).size(), 3);

    HistoryManager reloaded(path);
    reloaded.setMaxRecords(500);
    QCOMPARE(reloaded.records().size(), 3);
    QCOMPARE(reloaded.records().first().source, QStringLiteral("s4"));
}

// Records a lower limit drops are announced to the dialog that shows them.
void TestHistoryManager::announcesRecordsDroppedByALowerLimit()
{
    QDir().mkpath(TestSupport::tempDir());
    const QString path = historyPath();

    HistoryManager manager(path);
    manager.setMaxRecords(5);
    for (int i = 0; i < 5; ++i)
        manager.addRecord(record(i));

    QSignalSpy changedSpy(&manager, &HistoryManager::changed);
    // A limit that drops nothing is not a change.
    manager.setMaxRecords(5);
    QCOMPARE(changedSpy.count(), 0);

    manager.setMaxRecords(2);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(manager.records().size(), 2);

    // Nor is one that drops nothing more.
    manager.setMaxRecords(2);
    QCOMPARE(changedSpy.count(), 1);
}

// A record reaches the file as it arrives, so a crash cannot lose one.
void TestHistoryManager::appendsRecordsAsTheyArrive()
{
    QDir().mkpath(TestSupport::tempDir());
    const QString path = historyPath();

    HistoryManager manager(path);
    manager.setMaxRecords(10);
    manager.addRecord(record(0));
    QCOMPARE(fileLines(path).size(), 1);

    HistoryManager reloaded(path);
    QCOMPARE(reloaded.records().size(), 1);
    QCOMPARE(reloaded.records().first().source, QStringLiteral("s0"));
    QCOMPARE(reloaded.records().first().target, QStringLiteral("t0"));
}

// Appends pile up until the file outgrows the cap, so a steady history costs one line per record.
void TestHistoryManager::compactsOnceTheFileOutgrowsTheCap()
{
    QDir().mkpath(TestSupport::tempDir());
    const QString path = historyPath();

    HistoryManager manager(path);
    manager.setMaxRecords(8);
    for (int i = 0; i < 12; ++i)
        manager.addRecord(record(i));
    QCOMPARE(fileLines(path).size(), 12);

    manager.flush();
    const QStringList lines = fileLines(path);
    QCOMPARE(lines.size(), 8);
    QVERIFY(lines.first().contains(QStringLiteral("\"source\":\"s4\"")));
    QVERIFY(lines.last().contains(QStringLiteral("\"source\":\"s11\"")));
}

// The rewrite runs on the debounce, without anyone calling flush().
void TestHistoryManager::compactsOnTheDebounce()
{
    QDir().mkpath(TestSupport::tempDir());
    const QString path = historyPath();

    HistoryManager manager(path);
    manager.setMaxRecords(8);
    for (int i = 0; i < 12; ++i)
        manager.addRecord(record(i));
    QCOMPARE(fileLines(path).size(), 12);

    QTRY_COMPARE_WITH_TIMEOUT(fileLines(path).size(), 8, 2000);
}

// A line a crash cut short is dropped instead of failing the whole file.
void TestHistoryManager::skipsATruncatedLine()
{
    QDir().mkpath(TestSupport::tempDir());
    const QString path = historyPath();

    {
        HistoryManager manager(path);
        manager.setMaxRecords(10);
        manager.addRecord(record(0));
        manager.addRecord(record(1));
    }
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
        file.write(QByteArrayLiteral("{\"timestamp\":3,\"source\":\"s2\""));
    }

    HistoryManager reloaded(path);
    QCOMPARE(reloaded.records().size(), 2);
    QCOMPARE(reloaded.records().first().source, QStringLiteral("s1"));
}

QTEST_MAIN(TestHistoryManager)
#include "tst_historymanager.moc"
