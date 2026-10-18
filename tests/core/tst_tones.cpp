#include <QSet>
#include <QtTest>

#include "core/translation/Tone.h"

class TestTones : public QObject
{
    Q_OBJECT

private slots:
    void exposesPresets();
    void resolvesPresetLabels();
    void parsesCustomTones();
    void completesHalfFilledCustomTones();
    void dropsCollidingCustomTones();
    void roundTripsCustomTones();
};

void TestTones::exposesPresets()
{
    QSet<QString> keys;
    for (const ToneItem& tone : Tones::presets()) {
        QVERIFY2(tone.en && *tone.en, qPrintable(tone.key));
        QVERIFY2(!keys.contains(tone.key), qPrintable(tone.key));
        keys.insert(tone.key);
    }
    QVERIFY(keys.contains(QStringLiteral("default")));
}

void TestTones::resolvesPresetLabels()
{
    // The UI resolves these through the "Tones" catalog.
    QCOMPARE(QString::fromUtf8(Tones::labelFor(QStringLiteral("formal"))), QStringLiteral("Formal"));
    QVERIFY(Tones::labelFor(QStringLiteral("not-a-tone")) == nullptr);
    QVERIFY(Tones::labelFor(QString()) == nullptr);
}

void TestTones::parsesCustomTones()
{
    using namespace Tones;
    const QJsonArray stored = {
        QJsonObject{{QStringLiteral("key"), QStringLiteral("  pirate  ")},
                    {QStringLiteral("name"), QStringLiteral("  Pirate  ")}},
        QJsonObject{{QStringLiteral("key"), QStringLiteral("x-nameless")},
                    {QStringLiteral("name"), QStringLiteral("   ")}},
        QJsonObject{{QStringLiteral("key"), QStringLiteral("pirate")},
                    {QStringLiteral("name"), QStringLiteral("Duplicate key")}},
        QJsonObject{{QStringLiteral("name"), QStringLiteral("formal")}},
        QJsonValue(QStringLiteral("not-an-object")),
        QJsonObject{{QStringLiteral("key"), QStringLiteral("x-last")},
                    {QStringLiteral("name"), QStringLiteral("Last")}}
    };

    // The duplicate and the name deriving a preset key are dropped too.
    const QVector<CustomTone> parsed = customFromJson(stored);
    QCOMPARE(parsed.size(), 3);
    QCOMPARE(parsed.at(0).key, QStringLiteral("pirate"));
    QCOMPARE(parsed.at(0).name, QStringLiteral("Pirate"));
    QCOMPARE(parsed.at(1).key, QStringLiteral("x-nameless"));
    QCOMPARE(parsed.at(1).name, QStringLiteral("x-nameless"));
    QCOMPARE(parsed.at(2).key, QStringLiteral("x-last"));
    QCOMPARE(parsed.at(2).name, QStringLiteral("Last"));

    QVERIFY(customFromJson(QJsonArray()).isEmpty());
}

void TestTones::completesHalfFilledCustomTones()
{
    using namespace Tones;
    const QVector<CustomTone> normalized = customNormalized({
        {QStringLiteral("  pirate  "), QStringLiteral("  Pirate  ")},
        {QStringLiteral("x-nameless"), QString()},
        {QString(), QStringLiteral("Keyless")},
        {QStringLiteral("   "), QStringLiteral("   ")}
    });
    QCOMPARE(normalized.size(), 3);
    QCOMPARE(normalized.at(0).key, QStringLiteral("pirate"));
    QCOMPARE(normalized.at(0).name, QStringLiteral("Pirate"));
    QCOMPARE(normalized.at(1).key, QStringLiteral("x-nameless"));
    QCOMPARE(normalized.at(1).name, QStringLiteral("x-nameless"));
    QCOMPARE(normalized.at(2).key, QStringLiteral("Keyless"));
    QCOMPARE(normalized.at(2).name, QStringLiteral("Keyless"));
}

void TestTones::dropsCollidingCustomTones()
{
    using namespace Tones;
    const QVector<CustomTone> normalized = customNormalized({
        {QStringLiteral("pirate"), QStringLiteral("Pirate")},
        {QStringLiteral("pirate"), QStringLiteral("Duplicate")},
        {QStringLiteral("formal"), QStringLiteral("Formal custom")},
        {QString(), QStringLiteral("formal")},
        {QStringLiteral("x-custom"), QStringLiteral("Custom")}
    });
    QCOMPARE(normalized.size(), 2);
    QCOMPARE(normalized.at(0).key, QStringLiteral("pirate"));
    QCOMPARE(normalized.at(0).name, QStringLiteral("Pirate"));
    QCOMPARE(normalized.at(1).key, QStringLiteral("x-custom"));
    QCOMPARE(normalized.at(1).name, QStringLiteral("Custom"));
}

void TestTones::roundTripsCustomTones()
{
    using namespace Tones;
    const QVector<CustomTone> custom = {
        {QStringLiteral("pirate"), QStringLiteral("Pirate")},
        {QStringLiteral("polite"), QStringLiteral("Polite")}
    };

    const QJsonArray stored = customToJson(custom);
    QCOMPARE(stored.size(), 2);
    QCOMPARE(stored.at(0).toObject().keys(),
             QStringList({QStringLiteral("key"), QStringLiteral("name")}));
    QCOMPARE(stored.at(0).toObject().value(QStringLiteral("key")).toString(),
             QStringLiteral("pirate"));
    QCOMPARE(stored.at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("Pirate"));

    const QVector<CustomTone> parsed = customFromJson(stored);
    QCOMPARE(parsed.size(), custom.size());
    for (qsizetype index = 0; index < custom.size(); ++index) {
        QCOMPARE(parsed.at(index).key, custom.at(index).key);
        QCOMPARE(parsed.at(index).name, custom.at(index).name);
    }

    QVERIFY(customToJson({}).isEmpty());
    QVERIFY(customFromJson(customToJson({})).isEmpty());
}

QTEST_MAIN(TestTones)
#include "tst_tones.moc"
