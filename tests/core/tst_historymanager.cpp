#include <QtTest>

#include "TestSupport.h"
#include "core/history/HistoryManager.h"

#include <QFileInfo>

class TestHistoryManager : public QObject
{
    Q_OBJECT

private slots:
    void trimsToMaxRecords();
    void debouncesSavesUntilFlush();
};

void TestHistoryManager::trimsToMaxRecords()
{
    QDir().mkpath(TestSupport::tempDir());
    const QString path = TestSupport::tempDir() + QStringLiteral("/history.json");

    {
        HistoryManager manager(path);
        manager.setMaxRecords(3);
        for (int i = 0; i < 5; ++i) {
            TranslationRecord record;
            record.timestamp = i + 1;
            record.source = QStringLiteral("s%1").arg(i);
            record.target = QStringLiteral("t%1").arg(i);
            record.sourceLang = QStringLiteral("auto");
            record.targetLang = QStringLiteral("en");
            manager.addRecord(record);
        }
        QCOMPARE(manager.records().size(), 3);
        QCOMPARE(manager.records().first().source, QStringLiteral("s4"));
        QCOMPARE(manager.records().last().source, QStringLiteral("s2"));
    }

    HistoryManager reloaded(path);
    reloaded.setMaxRecords(500);
    QCOMPARE(reloaded.records().size(), 3);
}

void TestHistoryManager::debouncesSavesUntilFlush()
{
    QDir().mkpath(TestSupport::tempDir());
    const QString path = TestSupport::tempDir() + QStringLiteral("/history.json");

    HistoryManager manager(path);
    TranslationRecord record;
    record.timestamp = 1;
    record.source = QStringLiteral("hello");
    record.target = QStringLiteral("你好");
    record.sourceLang = QStringLiteral("auto");
    record.targetLang = QStringLiteral("zh");
    manager.addRecord(record);

    QVERIFY(!QFileInfo::exists(path));

    manager.flush();

    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
        QCOMPARE(array.size(), 1);
        QCOMPARE(array.first().toObject().value(QStringLiteral("source")).toString(),
                 QStringLiteral("hello"));
    }
}

QTEST_MAIN(TestHistoryManager)
#include "tst_historymanager.moc"
