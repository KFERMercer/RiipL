#include <QtTest>

#include "TestSupport.h"
#include "core/config/ApiPreset.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

class TestApiPresets : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsThroughJson();
    void looksUpAndMatches();
    void matchesAppliedSettings();
    void applyWritesEveryField();
};

void TestApiPresets::roundTripsThroughJson()
{
    ApiPreset preset;
    preset.name = QStringLiteral("OpenAI");
    preset.values.insert(Keys::apiBaseUrl, QStringLiteral("https://api.openai.com/v1"));
    preset.values.insert(Keys::apiModel, QStringLiteral("gpt-4o-mini"));
    preset.values.insert(Keys::apiMaxTokens, 2048);

    const QJsonArray encoded = ApiPresets::toJson({preset});
    const QVector<ApiPreset> restored = ApiPresets::fromJson(encoded);
    QCOMPARE(restored.size(), 1);
    QCOMPARE(restored.first().name, QStringLiteral("OpenAI"));
    QCOMPARE(restored.first().values.value(Keys::apiBaseUrl).toString(),
             QStringLiteral("https://api.openai.com/v1"));
    // Values keep their JSON types across the round trip.
    QCOMPARE(restored.first().values.value(Keys::apiMaxTokens).toInt(), 2048);

    // Entries without a usable name are dropped rather than kept as blanks.
    const QJsonArray malformed = QJsonArray{
        QJsonObject{{QStringLiteral("name"), QStringLiteral("  ")}},
        QJsonObject{{QStringLiteral("values"), QJsonObject()}},
        QJsonValue(42),
    };
    QCOMPARE(ApiPresets::fromJson(malformed).size(), 0);
    QCOMPARE(ApiPresets::fromJson(QJsonArray()).size(), 0);
}

void TestApiPresets::looksUpAndMatches()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    ApiPreset openai;
    openai.name = QStringLiteral("OpenAI");
    openai.values.insert(Keys::apiBaseUrl, QStringLiteral("https://api.openai.com/v1"));
    openai.values.insert(Keys::apiKey, QStringLiteral("sk-openai"));
    openai.values.insert(Keys::apiModel, QStringLiteral("gpt-4o-mini"));

    ApiPreset local;
    local.name = QStringLiteral("Local");
    local.values.insert(Keys::apiBaseUrl, QStringLiteral("http://localhost:11434"));
    local.values.insert(Keys::apiModel, QStringLiteral("qwen2.5"));

    const QVector<ApiPreset> presets = {openai, local};
    QCOMPARE(ApiPresets::indexOf(presets, QStringLiteral("Local")), 1);
    QCOMPARE(ApiPresets::indexOf(presets, QStringLiteral("Missing")), -1);

    // A preset matches the settings only while every captured field agrees.
    QCOMPARE(ApiPresets::matchValues(presets, openai.values), 0);
    QJsonObject drifted = openai.values;
    drifted.insert(Keys::apiModel, QStringLiteral("gpt-4o"));
    QCOMPARE(ApiPresets::matchValues(presets, drifted), -1);
    // Changing a field no preset captured still breaks the match.
    drifted = openai.values;
    drifted.insert(Keys::apiTemperature, 0.5);
    QCOMPARE(ApiPresets::matchValues(presets, drifted), -1);

    // A field a preset does not carry is compared against its default, so an
    // omitted field does not make the preset match unconditionally.
    QCOMPARE(presets.at(1).values.contains(Keys::apiTemperature), false);
    QCOMPARE(ApiPresets::matchValues({local},
                                     QJsonObject{{Keys::apiBaseUrl, QStringLiteral("http://localhost:11434")},
                                                 {Keys::apiModel, QStringLiteral("qwen2.5")}}),
             0);
    QCOMPARE(ApiPresets::matchValues({local},
                                     QJsonObject{{Keys::apiBaseUrl, QStringLiteral("http://localhost:11434")},
                                                 {Keys::apiModel, QStringLiteral("qwen2.5")},
                                                 {Keys::apiTemperature, 0.7}}),
             -1);

    // Each captured field is compared by its own type, not by string form.
    ApiPreset typed;
    typed.name = QStringLiteral("Typed");
    typed.values.insert(Keys::apiMaxTokens, 4096);
    QCOMPARE(ApiPresets::matchValues({typed}, QJsonObject{{Keys::apiMaxTokens, 4096}}), 0);
    QCOMPARE(ApiPresets::matchValues({typed},
                                     QJsonObject{{Keys::apiMaxTokens, QStringLiteral("4096")}}),
             -1);

    QCOMPARE(ApiPresets::matchValues({}, openai.values), -1);
}

// The management window highlights the preset that the applied settings match,
// so matching against the configuration has to identify it exactly.

void TestApiPresets::matchesAppliedSettings()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    ConfigManager* config = ConfigManager::instance();

    ApiPreset openai;
    openai.name = QStringLiteral("OpenAI");
    openai.values.insert(Keys::apiBaseUrl, QStringLiteral("https://api.openai.com/v1"));
    openai.values.insert(Keys::apiKey, QStringLiteral("sk-openai"));
    openai.values.insert(Keys::apiModel, QStringLiteral("gpt-4o-mini"));

    ApiPreset local;
    local.name = QStringLiteral("Local");
    local.values.insert(Keys::apiBaseUrl, QStringLiteral("http://localhost:11434"));
    local.values.insert(Keys::apiModel, QStringLiteral("qwen2.5"));

    const QVector<ApiPreset> presets = {openai, local};

    QCOMPARE(ApiPresets::matchValues(presets, ApiPresets::capture()), -1);

    ApiPresets::apply(local);
    QCOMPARE(ApiPresets::matchValues(presets, ApiPresets::capture()), 1);
    QCOMPARE(ApiPresets::capture().value(Keys::apiModel).toString(), QStringLiteral("qwen2.5"));

    ApiPresets::apply(openai);
    QCOMPARE(ApiPresets::matchValues(presets, ApiPresets::capture()), 0);

    // Editing one applied field breaks the match instead of staying ambiguous.
    config->setValue(Keys::apiModel, QStringLiteral("gpt-4o"));
    QCOMPARE(ApiPresets::matchValues(presets, ApiPresets::capture()), -1);

    // capture() reads every captured field, so a stray value in any of them is
    // reported rather than silently matching the preset.
    ApiPresets::apply(openai);
    QCOMPARE(ApiPresets::matchValues(presets, ApiPresets::capture()), 0);
    QCOMPARE(ApiPresets::capture().size(), Keys::apiPresetFields().size());
    config->setValue(Keys::apiExtraBody, QStringLiteral("{\"top_p\":0.1}"));
    QCOMPARE(ApiPresets::matchValues(presets, ApiPresets::capture()), -1);
}

void TestApiPresets::applyWritesEveryField()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    ConfigManager* config = ConfigManager::instance();

    config->setValue(Keys::apiBaseUrl, QStringLiteral("http://example.invalid"));
    config->setValue(Keys::apiKey, QStringLiteral("stale"));
    config->setValue(Keys::apiModel, QStringLiteral("stale-model"));
    config->setValue(Keys::apiMaxTokens, 1);
    config->setValue(Keys::apiTemperature, 1.5);
    config->setValue(Keys::apiMaxConcurrency, 64);
    config->setValue(Keys::apiStream, false);
    config->setValue(Keys::apiExtraBody, QStringLiteral("{\"x\":1}"));
    config->setValue(Keys::apiCustomHeaders, QStringLiteral("X-Stale: 1"));

    ApiPreset preset;
    preset.name = QStringLiteral("DeepSeek");
    preset.values.insert(Keys::apiBaseUrl, QStringLiteral("https://api.deepseek.com/v1"));
    preset.values.insert(Keys::apiKey, QStringLiteral("sk-deepseek"));
    preset.values.insert(Keys::apiModel, QStringLiteral("deepseek-chat"));
    preset.values.insert(Keys::apiMaxTokens, 8192);
    preset.values.insert(Keys::apiTemperature, 0.3);
    preset.values.insert(Keys::apiMaxConcurrency, 8);
    preset.values.insert(Keys::apiStream, true);
    preset.values.insert(Keys::apiExtraBody, QStringLiteral("{\"top_p\":0.9}"));
    preset.values.insert(Keys::apiCustomHeaders, QStringLiteral("X-Custom: yes"));

    ApiPresets::apply(preset);

    QCOMPARE(config->stringValue(Keys::apiBaseUrl), QStringLiteral("https://api.deepseek.com/v1"));
    QCOMPARE(config->stringValue(Keys::apiKey), QStringLiteral("sk-deepseek"));
    QCOMPARE(config->stringValue(Keys::apiModel), QStringLiteral("deepseek-chat"));
    QCOMPARE(config->intValue(Keys::apiMaxTokens), 8192);
    QCOMPARE(config->doubleValue(Keys::apiTemperature), 0.3);
    QCOMPARE(config->boolValue(Keys::apiStream), true);
    QCOMPARE(config->intValue(Keys::apiMaxConcurrency), 8);
    QCOMPARE(config->stringValue(Keys::apiExtraBody), QStringLiteral("{\"top_p\":0.9}"));
    QCOMPARE(config->stringValue(Keys::apiCustomHeaders), QStringLiteral("X-Custom: yes"));

    QCOMPARE(ApiPresets::matchValues({preset},
                                     [&]() {
                                         QJsonObject values;
                                         for (const QString& key : Keys::apiPresetFields())
                                             values.insert(key, config->value(key));
                                         return values;
                                     }()),
             0);

    // Fields a preset omits return to their defaults instead of keeping the
    // previous provider's values.
    ApiPreset sparse;
    sparse.name = QStringLiteral("Sparse");
    sparse.values.insert(Keys::apiBaseUrl, QStringLiteral("https://sparse.example/v1"));
    ApiPresets::apply(sparse);
    QCOMPARE(config->stringValue(Keys::apiBaseUrl), QStringLiteral("https://sparse.example/v1"));
    QVERIFY(config->isDefault(Keys::apiModel));
    QVERIFY(config->isDefault(Keys::apiKey));
    QVERIFY(config->isDefault(Keys::apiMaxTokens));
    QVERIFY(config->isDefault(Keys::apiTemperature));
    QVERIFY(config->isDefault(Keys::apiMaxConcurrency));
    QVERIFY(config->isDefault(Keys::apiStream));
    QVERIFY(config->isDefault(Keys::apiCustomHeaders));

    config->setValue(Keys::apiPresets, ApiPresets::toJson({preset}));
    config->flush();
    const QVector<ApiPreset> reloaded =
        ApiPresets::fromJson(config->value(Keys::apiPresets).toArray());
    QCOMPARE(reloaded.size(), 1);
    QCOMPARE(reloaded.first().name, QStringLiteral("DeepSeek"));
    QCOMPARE(reloaded.first().values.value(Keys::apiKey).toString(), QStringLiteral("sk-deepseek"));
}

QTEST_MAIN(TestApiPresets)
#include "tst_apipresets.moc"
