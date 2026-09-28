#include <QtTest>

#include "TestSupport.h"
#include "core/models/Glossary.h"

class TestGlossary : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsThroughJson();
};

void TestGlossary::roundTripsThroughJson()
{
    QVector<GlossaryEntry> entries;
    entries.append({QStringLiteral("术语"), QStringLiteral("term")});
    entries.append({QStringLiteral("专有名词"), QString()});
    const QJsonArray array = Glossary::toJson(entries);
    const QVector<GlossaryEntry> restored = Glossary::fromJson(array);
    QCOMPARE(restored.size(), 2);
    QVERIFY(restored.first() == entries.first());
    QVERIFY(restored.last() == entries.last());
}

QTEST_MAIN(TestGlossary)
#include "tst_glossary.moc"
