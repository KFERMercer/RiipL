#include <QtTest>

#include "TestSupport.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

class TestConfigManager : public QObject
{
    Q_OBJECT

private slots:
    void fallsBackToDefaults();
    void storesOnlyNonDefaults();
    void persistsAcrossInstances();
    void flushesPendingSaveOnRecreate();
    void valueEqualityMatchesDefaults();
    void resolvesUiLanguage();
};

void TestConfigManager::fallsBackToDefaults()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    QCOMPARE(ConfigManager::instance()->stringValue(Keys::apiModel), Defaults::apiModel);
    QCOMPARE(ConfigManager::instance()->doubleValue(Keys::apiTemperature), Defaults::apiTemperature);
    QCOMPARE(ConfigManager::instance()->intValue(Keys::apiTimeoutMs), Defaults::apiTimeoutMs);
    QCOMPARE(ConfigManager::instance()->stringValue(Keys::translationTargetLang), QStringLiteral("zh"));
    QVERIFY(ConfigManager::instance()->isDefault(Keys::apiModel));
    QFile file(ConfigManager::instance()->configFilePath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(file.readAll()).object(), QJsonObject());
}

void TestConfigManager::storesOnlyNonDefaults()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    ConfigManager::instance()->setValue(Keys::apiTemperature, 0.7);
    QVERIFY(!ConfigManager::instance()->isDefault(Keys::apiTemperature));
    QCOMPARE(ConfigManager::instance()->doubleValue(Keys::apiTemperature), 0.7);

    ConfigManager::instance()->flush();
    {
        QFile file(ConfigManager::instance()->configFilePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject doc = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(doc.value(QStringLiteral("api")).toObject().value(QStringLiteral("temperature")).toDouble(), 0.7);
        QVERIFY(!doc.value(QStringLiteral("api")).toObject().contains(QStringLiteral("stream")));
    }

    ConfigManager::instance()->setValue(Keys::apiTemperature, Defaults::apiTemperature);
    ConfigManager::instance()->flush();
    {
        QFile file(ConfigManager::instance()->configFilePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject after = QJsonDocument::fromJson(file.readAll()).object();
        QVERIFY(!after.value(QStringLiteral("api")).toObject().contains(QStringLiteral("temperature")));
    }
    QVERIFY(ConfigManager::instance()->isDefault(Keys::apiTemperature));
}

void TestConfigManager::persistsAcrossInstances()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    ConfigManager::instance()->setValue(Keys::uiLanguage, QStringLiteral("zh"));
    ConfigManager::instance()->setValue(Keys::translationTargetLang, QStringLiteral("ja"));
    ConfigManager::instance()->flush();

    ConfigManager::createInstance(TestSupport::tempDir());
    QCOMPARE(ConfigManager::instance()->stringValue(Keys::uiLanguage), QStringLiteral("zh"));
    QCOMPARE(ConfigManager::instance()->stringValue(Keys::translationTargetLang), QStringLiteral("ja"));
    QCOMPARE(ConfigManager::instance()->intValue(Keys::uiAutoTranslateDelay), 800);
}

void TestConfigManager::flushesPendingSaveOnRecreate()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    ConfigManager::instance()->setValue(Keys::translationTargetLang, QStringLiteral("ja"));
    ConfigManager::createInstance(TestSupport::tempDir());
    QCOMPARE(ConfigManager::instance()->stringValue(Keys::translationTargetLang),
             QStringLiteral("ja"));
}

void TestConfigManager::valueEqualityMatchesDefaults()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    ConfigManager* config = ConfigManager::instance();

    // Non-default values, including nested ones, are detected as changes.
    config->setValue(Keys::translationCustomTones,
                     QJsonArray{QJsonObject{{QStringLiteral("key"), QStringLiteral("x")}}});
    QVERIFY(!config->isDefault(Keys::translationCustomTones));
    config->setValue(Keys::translationCustomTones, config->value(Keys::translationCustomTones));
    QCOMPARE(config->value(Keys::translationCustomTones).toArray().size(), 1);

    config->setValue(Keys::translationCustomTones, QJsonArray());
    QVERIFY(config->isDefault(Keys::translationCustomTones));

    // A stored integer compares equal to the default that stores the same
    // number as a double, so the override is dropped instead of persisting.
    QCOMPARE(Defaults::value(Keys::uiFontSize).type(), QJsonValue::Double);
    config->setValue(Keys::uiFontSize, QJsonValue(int(Defaults::uiFontSize)));
    QVERIFY(config->isDefault(Keys::uiFontSize));
    QCOMPARE(config->value(Keys::uiFontSize).toInt(), Defaults::uiFontSize);

    config->setValue(Keys::uiFontSize, 14);
    QCOMPARE(config->value(Keys::uiFontSize).toInt(), 14);
    QVERIFY(!config->isDefault(Keys::uiFontSize));
    config->setValue(Keys::uiFontSize, Defaults::uiFontSize);
    QVERIFY(config->isDefault(Keys::uiFontSize));

    // Values of different JSON types are never treated as equal.
    config->setValue(Keys::apiKey, QJsonValue(QStringLiteral("10")));
    QVERIFY(!config->isDefault(Keys::apiKey));
}

void TestConfigManager::resolvesUiLanguage()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    QVERIFY(ConfigManager::instance()->isDefault(Keys::uiLanguage));

    const QString resolved = ConfigManager::instance()->resolvedUiLanguage();
    QVERIFY(resolved == QLatin1String("en") || resolved == QLatin1String("zh"));

    ConfigManager::instance()->setValue(Keys::uiLanguage, QStringLiteral("zh"));
    QCOMPARE(ConfigManager::instance()->resolvedUiLanguage(), QStringLiteral("zh"));
    ConfigManager::instance()->setValue(Keys::uiLanguage, QStringLiteral("en"));
    QCOMPARE(ConfigManager::instance()->resolvedUiLanguage(), QStringLiteral("en"));
}

QTEST_MAIN(TestConfigManager)
#include "tst_configmanager.moc"