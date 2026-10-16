#include <QtTest>

#include "TestSupport.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/config/Preset.h"

class TestPreset : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsThroughJson();
    void fillsMissingFieldsFromDefaults();
    void capturesAndAppliesFields();
    void looksUpAndMatches();
    void matchesAppliedApiFields();
    void writesEveryApiPresetField();
};

void TestPreset::roundTripsThroughJson()
{
    Preset preset;
    preset.name = QStringLiteral("Formal");
    preset.values.insert(Keys::translationStyle, QStringLiteral("formal"));
    preset.values.insert(Keys::translationBackground, QStringLiteral("Legal contract"));

    const QJsonArray encoded = Presets::toJson({preset});
    const QVector<Preset> restored = Presets::fromJson(encoded);
    QCOMPARE(restored.size(), 1);
    QCOMPARE(restored.first().name, QStringLiteral("Formal"));
    QCOMPARE(restored.first().values.value(Keys::translationStyle).toString(),
             QStringLiteral("formal"));

    const QJsonArray malformed = QJsonArray{
        QJsonObject{{QStringLiteral("name"), QStringLiteral("  ")}},
        QJsonObject{{QStringLiteral("values"), QJsonObject()}},
        QJsonValue(42),
    };
    QCOMPARE(Presets::fromJson(malformed).size(), 0);
    QCOMPARE(Presets::fromJson(QJsonArray()).size(), 0);

    // A name is trimmed on the way in, so stored blanks never become presets.
    const QJsonArray padded = QJsonArray{
        QJsonObject{{QStringLiteral("name"), QStringLiteral("  Spaced  ")}},
    };
    QCOMPARE(Presets::fromJson(padded).first().name, QStringLiteral("Spaced"));
}

void TestPreset::fillsMissingFieldsFromDefaults()
{
    const QStringList fields = Keys::contextPresetFields();
    const QJsonObject filled = Presets::withDefaults(QJsonObject(), fields);
    QCOMPARE(filled.size(), fields.size());
    for (const QString& key : fields)
        QCOMPARE(filled.value(key), Defaults::value(key));

    const QJsonObject partial = Presets::withDefaults(
        QJsonObject{{Keys::translationStyle, QStringLiteral("casual")}}, fields);
    QCOMPARE(partial.value(Keys::translationStyle).toString(), QStringLiteral("casual"));
    QCOMPARE(partial.value(Keys::translationBackground), Defaults::value(Keys::translationBackground));

    // A stored value wins over the default even when it is empty.
    const QJsonObject empty = Presets::withDefaults(
        QJsonObject{{Keys::translationStyle, QString()}}, fields);
    QCOMPARE(empty.value(Keys::translationStyle).toString(), QString());
}

void TestPreset::capturesAndAppliesFields()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    ConfigManager* config = ConfigManager::instance();

    const QStringList fields = Keys::contextPresetFields();
    QCOMPARE(Presets::capture(fields).size(), fields.size());

    Preset preset;
    preset.name = QStringLiteral("Medical");
    preset.values.insert(Keys::translationStyle, QStringLiteral("clinical"));
    preset.values.insert(Keys::translationBackground, QStringLiteral("Clinical report"));

    Presets::apply(preset, fields);
    QCOMPARE(config->stringValue(Keys::translationStyle), QStringLiteral("clinical"));
    QCOMPARE(config->stringValue(Keys::translationBackground), QStringLiteral("Clinical report"));
    QCOMPARE(Presets::matchValues({preset}, Presets::capture(fields), fields), 0);

    // Fields the preset omits return to their defaults rather than keeping the
    // previous context.
    Preset sparse;
    sparse.name = QStringLiteral("Sparse");
    sparse.values.insert(Keys::translationStyle, QStringLiteral("terse"));
    Presets::apply(sparse, fields);
    QCOMPARE(config->stringValue(Keys::translationStyle), QStringLiteral("terse"));
    QVERIFY(config->isDefault(Keys::translationBackground));
    QCOMPARE(Presets::matchValues({preset}, Presets::capture(fields), fields), -1);
}

void TestPreset::looksUpAndMatches()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());

    const QStringList fields = {Keys::translationStyle, Keys::translationBackground};

    Preset formal;
    formal.name = QStringLiteral("Formal");
    formal.values.insert(Keys::translationStyle, QStringLiteral("formal"));

    Preset casual;
    casual.name = QStringLiteral("Casual");
    casual.values.insert(Keys::translationStyle, QStringLiteral("casual"));

    const QVector<Preset> presets = {formal, casual};
    QCOMPARE(Presets::indexOf(presets, QStringLiteral("Casual")), 1);
    QCOMPARE(Presets::indexOf(presets, QStringLiteral("Missing")), -1);

    QCOMPARE(Presets::matchValues(presets, formal.values, fields), 0);

    // A field outside the captured list never decides the match.
    QJsonObject drifted = formal.values;
    drifted.insert(Keys::apiModel, QStringLiteral("gpt-4o"));
    QCOMPARE(Presets::matchValues(presets, drifted, fields), 0);

    // A captured field the values omit is compared against its default.
    QCOMPARE(Presets::matchValues({casual},
                                  QJsonObject{{Keys::translationStyle, QStringLiteral("casual")}},
                                  fields),
             0);
    QCOMPARE(Presets::matchValues({casual},
                                  QJsonObject{{Keys::translationStyle, QStringLiteral("casual")},
                                              {Keys::translationBackground, QStringLiteral("x")}},
                                  fields),
             -1);

    QCOMPARE(Presets::matchValues({}, formal.values, fields), -1);
}

void TestPreset::matchesAppliedApiFields()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    ConfigManager* config = ConfigManager::instance();
    const QStringList fields = Keys::apiPresetFields();

    Preset openai;
    openai.name = QStringLiteral("OpenAI");
    openai.values.insert(Keys::apiBaseUrl, QStringLiteral("https://api.openai.com/v1"));
    openai.values.insert(Keys::apiKey, QStringLiteral("sk-openai"));
    openai.values.insert(Keys::apiModel, QStringLiteral("gpt-4o-mini"));

    Preset local;
    local.name = QStringLiteral("Local");
    local.values.insert(Keys::apiBaseUrl, QStringLiteral("http://localhost:11434"));
    local.values.insert(Keys::apiModel, QStringLiteral("qwen2.5"));

    const QVector<Preset> presets = {openai, local};

    QCOMPARE(Presets::matchValues(presets, Presets::capture(fields), fields), -1);

    Presets::apply(local, fields);
    QCOMPARE(Presets::matchValues(presets, Presets::capture(fields), fields), 1);
    QCOMPARE(Presets::capture(fields).value(Keys::apiModel).toString(), QStringLiteral("qwen2.5"));

    Presets::apply(openai, fields);
    QCOMPARE(Presets::matchValues(presets, Presets::capture(fields), fields), 0);

    // Editing one applied field breaks the match instead of staying ambiguous.
    config->setValue(Keys::apiModel, QStringLiteral("gpt-4o"));
    QCOMPARE(Presets::matchValues(presets, Presets::capture(fields), fields), -1);

    // capture() reads every captured field, so a stray value in any of them is
    // reported rather than silently matching the preset.
    Presets::apply(openai, fields);
    QCOMPARE(Presets::matchValues(presets, Presets::capture(fields), fields), 0);
    QCOMPARE(Presets::capture(fields).size(), fields.size());
    config->setValue(Keys::apiExtraBody, QStringLiteral("{\"top_p\":0.1}"));
    QCOMPARE(Presets::matchValues(presets, Presets::capture(fields), fields), -1);

    // Each captured field is compared by its own type, not by string form.
    Preset typed;
    typed.name = QStringLiteral("Typed");
    typed.values.insert(Keys::apiMaxTokens, 4096);
    QCOMPARE(Presets::matchValues({typed}, QJsonObject{{Keys::apiMaxTokens, 4096}}, fields), 0);
    QCOMPARE(Presets::matchValues({typed}, QJsonObject{{Keys::apiMaxTokens, QStringLiteral("4096")}},
                                  fields),
             -1);
}

void TestPreset::writesEveryApiPresetField()
{
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    ConfigManager* config = ConfigManager::instance();
    const QStringList fields = Keys::apiPresetFields();

    config->setValue(Keys::apiBaseUrl, QStringLiteral("http://example.invalid"));
    config->setValue(Keys::apiKey, QStringLiteral("stale"));
    config->setValue(Keys::apiModel, QStringLiteral("stale-model"));
    config->setValue(Keys::apiMaxTokens, 1);
    config->setValue(Keys::apiTemperature, 1.5);
    config->setValue(Keys::apiMaxConcurrency, 64);
    config->setValue(Keys::apiStream, false);
    config->setValue(Keys::apiExtraBody, QStringLiteral("{\"x\":1}"));
    config->setValue(Keys::apiCustomHeaders, QStringLiteral("X-Stale: 1"));

    Preset preset;
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

    Presets::apply(preset, fields);

    QCOMPARE(config->stringValue(Keys::apiBaseUrl), QStringLiteral("https://api.deepseek.com/v1"));
    QCOMPARE(config->stringValue(Keys::apiKey), QStringLiteral("sk-deepseek"));
    QCOMPARE(config->stringValue(Keys::apiModel), QStringLiteral("deepseek-chat"));
    QCOMPARE(config->intValue(Keys::apiMaxTokens), 8192);
    QCOMPARE(config->doubleValue(Keys::apiTemperature), 0.3);
    QCOMPARE(config->boolValue(Keys::apiStream), true);
    QCOMPARE(config->intValue(Keys::apiMaxConcurrency), 8);
    QCOMPARE(config->stringValue(Keys::apiExtraBody), QStringLiteral("{\"top_p\":0.9}"));
    QCOMPARE(config->stringValue(Keys::apiCustomHeaders), QStringLiteral("X-Custom: yes"));
    QCOMPARE(Presets::matchValues({preset}, Presets::capture(fields), fields), 0);

    // Fields a preset omits return to their defaults instead of keeping the
    // previous provider's values.
    Preset sparse;
    sparse.name = QStringLiteral("Sparse");
    sparse.values.insert(Keys::apiBaseUrl, QStringLiteral("https://sparse.example/v1"));
    Presets::apply(sparse, fields);
    QCOMPARE(config->stringValue(Keys::apiBaseUrl), QStringLiteral("https://sparse.example/v1"));
    QVERIFY(config->isDefault(Keys::apiModel));
    QVERIFY(config->isDefault(Keys::apiKey));
    QVERIFY(config->isDefault(Keys::apiMaxTokens));
    QVERIFY(config->isDefault(Keys::apiTemperature));
    QVERIFY(config->isDefault(Keys::apiMaxConcurrency));
    QVERIFY(config->isDefault(Keys::apiStream));
    QVERIFY(config->isDefault(Keys::apiCustomHeaders));

    config->setValue(Keys::apiPresets, Presets::toJson({preset}));
    config->flush();
    const QVector<Preset> reloaded = Presets::fromJson(config->value(Keys::apiPresets).toArray());
    QCOMPARE(reloaded.size(), 1);
    QCOMPARE(reloaded.first().name, QStringLiteral("DeepSeek"));
    QCOMPARE(reloaded.first().values.value(Keys::apiKey).toString(), QStringLiteral("sk-deepseek"));
}

QTEST_MAIN(TestPreset)
#include "tst_preset.moc"
