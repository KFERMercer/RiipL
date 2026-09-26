#pragma once

#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace Prompts {

// Canonical prompt template identifiers from which per-language config keys derive.
// Each template owns its labels, fences and placeholders; PromptBuilder only
// orders them and drops the ones whose variable is empty. The declaration order
// mirrors the order the fragments reach the model.
inline const QString systemTemplate = QStringLiteral("system");
inline const QString referenceTemplate = QStringLiteral("reference");
inline const QString toneTemplate = QStringLiteral("tone");
inline const QString styleTemplate = QStringLiteral("style");
inline const QString backgroundTemplate = QStringLiteral("background");
inline const QString glossaryTemplate = QStringLiteral("glossary");
inline const QString defaultTemplate = QStringLiteral("default");
inline const QString candidateTemplate = QStringLiteral("candidate");

}

namespace Keys {

// Single construction point for per-language prompt template keys.
inline QString promptKey(const QString& name, const QString& language)
{
    return QStringLiteral("prompts.%1_%2").arg(name, language);
}

inline const QString promptSystemZh = promptKey(Prompts::systemTemplate, QStringLiteral("zh"));
inline const QString promptSystemEn = promptKey(Prompts::systemTemplate, QStringLiteral("en"));
inline const QString promptReferenceZh = promptKey(Prompts::referenceTemplate, QStringLiteral("zh"));
inline const QString promptReferenceEn = promptKey(Prompts::referenceTemplate, QStringLiteral("en"));
inline const QString promptToneZh = promptKey(Prompts::toneTemplate, QStringLiteral("zh"));
inline const QString promptToneEn = promptKey(Prompts::toneTemplate, QStringLiteral("en"));
inline const QString promptStyleZh = promptKey(Prompts::styleTemplate, QStringLiteral("zh"));
inline const QString promptStyleEn = promptKey(Prompts::styleTemplate, QStringLiteral("en"));
inline const QString promptBackgroundZh = promptKey(Prompts::backgroundTemplate, QStringLiteral("zh"));
inline const QString promptBackgroundEn = promptKey(Prompts::backgroundTemplate, QStringLiteral("en"));
inline const QString promptGlossaryZh = promptKey(Prompts::glossaryTemplate, QStringLiteral("zh"));
inline const QString promptGlossaryEn = promptKey(Prompts::glossaryTemplate, QStringLiteral("en"));
inline const QString promptDefaultZh = promptKey(Prompts::defaultTemplate, QStringLiteral("zh"));
inline const QString promptDefaultEn = promptKey(Prompts::defaultTemplate, QStringLiteral("en"));
inline const QString promptCandidateZh = promptKey(Prompts::candidateTemplate, QStringLiteral("zh"));
inline const QString promptCandidateEn = promptKey(Prompts::candidateTemplate, QStringLiteral("en"));

inline const QString apiBaseUrl = QStringLiteral("api.base_url");
inline const QString apiKey = QStringLiteral("api.api_key");
inline const QString apiModel = QStringLiteral("api.model");
inline const QString apiTimeoutMs = QStringLiteral("api.timeout_ms");
inline const QString apiTemperature = QStringLiteral("api.temperature");
inline const QString apiMaxTokens = QStringLiteral("api.max_tokens");
inline const QString apiStream = QStringLiteral("api.stream");
inline const QString apiExtraBody = QStringLiteral("api.extra_body");
inline const QString apiCustomHeaders = QStringLiteral("api.custom_headers");
inline const QString apiPresets = QStringLiteral("api.presets");

// Fields a named API preset captures, in settings-page order.
inline const QStringList& apiPresetFields()
{
    static const QStringList fields = {
        apiBaseUrl, apiKey, apiModel, apiTimeoutMs, apiTemperature,
        apiMaxTokens, apiStream, apiExtraBody, apiCustomHeaders,
    };
    return fields;
}

inline const QString uiLanguage = QStringLiteral("ui.language");
inline const QString uiAutoTranslate = QStringLiteral("ui.auto_translate");
inline const QString uiAutoTranslateDelay = QStringLiteral("ui.auto_translate_delay");
inline const QString uiWindowGeometry = QStringLiteral("ui.window_geometry");
inline const QString uiAlwaysOnTop = QStringLiteral("ui.always_on_top");
inline const QString uiMinimizeToTray = QStringLiteral("ui.minimize_to_tray");
inline const QString uiFontSize = QStringLiteral("ui.font_size");

inline const QString translationSourceLang = QStringLiteral("translation.source_lang");
inline const QString translationTargetLang = QStringLiteral("translation.target_lang");
inline const QString translationTone = QStringLiteral("translation.tone");
inline const QString translationCustomTones = QStringLiteral("translation.custom_tones");
inline const QString translationStyle = QStringLiteral("translation.style");
inline const QString translationBackground = QStringLiteral("translation.background");

inline const QString glossaryEnabled = QStringLiteral("glossary.enabled");
inline const QString glossaryEntries = QStringLiteral("glossary.entries");

inline const QString clipboardMonitor = QStringLiteral("clipboard.monitor");
inline const QString clipboardDelayMs = QStringLiteral("clipboard.delay_ms");

inline const QString historyEnabled = QStringLiteral("history.enabled");
inline const QString historyMaxRecords = QStringLiteral("history.max_records");

}

namespace Defaults {

inline const QString apiBaseUrl = QStringLiteral("https://api.openai.com/v1");
inline const QString apiKey = QString();
inline const QString apiModel = QStringLiteral("gpt-4o-mini");
inline const int apiTimeoutMs = 10000;
// Negative values keep the temperature parameter out of API requests.
inline const double apiTemperature = -0.1;
inline const int apiMaxTokens = 4096;
inline const bool apiStream = true;
inline const QString apiExtraBody = QString();
inline const QString apiCustomHeaders = QString();

inline const QString uiLanguage = QStringLiteral("auto");
inline const bool uiAutoTranslate = false;
inline const int uiAutoTranslateDelay = 800;
inline const QString uiWindowGeometry = QString();
inline const bool uiAlwaysOnTop = false;
inline const bool uiMinimizeToTray = true;
inline const int uiFontSize = 10;

inline const QString translationSourceLang = QStringLiteral("auto");
inline const QString translationTargetLang = QStringLiteral("zh");
inline const QString translationTone = QStringLiteral("neutral");
inline const QString translationStyle = QString();
inline const QString translationBackground = QString();

inline const bool glossaryEnabled = false;

inline const bool clipboardMonitor = false;
inline const int clipboardDelayMs = 500;

inline const bool historyEnabled = true;
inline const int historyMaxRecords = 500;

// The default template definitions below follow the order the fragments reach
// the model: the system prompt leads the request, the reference block follows
// it, and the candidate wording prompt is a separate request.

inline const QString promptSystemZh = QStringLiteral("你是一位翻译专家。");
inline const QString promptSystemEn = QStringLiteral("You are a professional translator.");

// The reference templates are self-contained: each one renders its own label,
// fence and placeholder, and is emitted only while its variable holds a value.
inline const QString promptReferenceZh = R"TXT(你需要仔细阅读并严格遵守以下参考信息：
)TXT";
inline const QString promptReferenceEn = R"TXT(Read the following reference information carefully and follow it strictly:
)TXT";

inline const QString promptToneZh = R"TXT(- 语气：
  ```
  {tone}
  ```
)TXT";
inline const QString promptToneEn = R"TXT(- Tone:
  ```
  {tone}
  ```
)TXT";

inline const QString promptStyleZh = R"TXT(- 风格：
  ```
  {style}
  ```
)TXT";
inline const QString promptStyleEn = R"TXT(- Style:
  ```
  {style}
  ```
)TXT";

inline const QString promptBackgroundZh = R"TXT(- 背景信息：
  ```
  {background}
  ```
)TXT";
inline const QString promptBackgroundEn = R"TXT(- Background:
  ```
  {background}
  ```
)TXT";

inline const QString promptGlossaryZh = R"TXT(- 术语表：
  ```json
  {glossary}
  ```
)TXT";
inline const QString promptGlossaryEn = R"TXT(- Glossary:
  ```json
  {glossary}
  ```
)TXT";

inline const QString promptDefaultZh = R"TXT(根据以上参考信息，将以下文本翻译为 {target_lang}，注意**只需要输出翻译后的结果，不要额外解释**：

```
{source_text}
```)TXT";
inline const QString promptDefaultEn = R"TXT(Based on the reference information above, translate the following text into {target_lang}. Note that you must **only output the translated result without any additional explanation**:

```
{source_text}
```)TXT";

inline const QString promptCandidateZh = R"TXT(原文：
```
{source_text}
```
译文：
```
{translated_text}
```
用户在译文中选中了：`{selected_word}`

请结合上下文判断选中内容对应的完整词语或短语（必要时可向左右扩展为更完整的词），
并提供 2-4 个可直接替换该词语的备选表达。

重要：replace 必须能在译文中原样找到；replace 与所有 options 必须使用 {target_lang} 书写，
与译文语言保持一致，并保证替换回译文后语法通顺，禁止翻译成其他任何语言。

严格按以下 JSON 格式输出，禁止输出任何解释或代码块标记：
{{"replace": "译文中需要被替换的完整片段", "options": ["备选一", "备选二", "备选三"]}})TXT";
inline const QString promptCandidateEn = R"TXT(Source:
```
{source_text}
```
Translation:
```
{translated_text}
```
The user selected `{selected_word}` in the translation.

Determine the complete word or phrase that the selection corresponds to in the translation (expand to the left or right if needed),
then provide 2-4 alternative expressions that can directly replace it. The result must read naturally in context.

Important: "replace" must appear verbatim in the translation; "replace" and every entry in "options" MUST be written in {target_lang},
the same language as the translation. Never use any other language.

Output strictly in the following JSON format with no explanation and no code fences:
{{"replace": "the exact fragment in the translation to be replaced", "options": ["option 1", "option 2", "option 3"]}})TXT";

inline QJsonValue value(const QString& key)
{
    if (key == Keys::apiBaseUrl) return QJsonValue(apiBaseUrl);
    if (key == Keys::apiKey) return QJsonValue(apiKey);
    if (key == Keys::apiModel) return QJsonValue(apiModel);
    if (key == Keys::apiTimeoutMs) return QJsonValue(apiTimeoutMs);
    if (key == Keys::apiTemperature) return QJsonValue(apiTemperature);
    if (key == Keys::apiMaxTokens) return QJsonValue(apiMaxTokens);
    if (key == Keys::apiStream) return QJsonValue(apiStream);
    if (key == Keys::apiExtraBody) return QJsonValue(apiExtraBody);
    if (key == Keys::apiCustomHeaders) return QJsonValue(apiCustomHeaders);
    if (key == Keys::apiPresets) return QJsonArray();
    if (key == Keys::uiLanguage) return QJsonValue(uiLanguage);
    if (key == Keys::uiAutoTranslate) return QJsonValue(uiAutoTranslate);
    if (key == Keys::uiAutoTranslateDelay) return QJsonValue(uiAutoTranslateDelay);
    if (key == Keys::uiWindowGeometry) return QJsonValue(uiWindowGeometry);
    if (key == Keys::uiAlwaysOnTop) return QJsonValue(uiAlwaysOnTop);
    if (key == Keys::uiMinimizeToTray) return QJsonValue(uiMinimizeToTray);
    if (key == Keys::uiFontSize) return QJsonValue(uiFontSize);
    if (key == Keys::translationSourceLang) return QJsonValue(translationSourceLang);
    if (key == Keys::translationTargetLang) return QJsonValue(translationTargetLang);
    if (key == Keys::translationTone) return QJsonValue(translationTone);
    if (key == Keys::translationCustomTones) return QJsonArray();
    if (key == Keys::translationStyle) return QJsonValue(translationStyle);
    if (key == Keys::translationBackground) return QJsonValue(translationBackground);
    if (key == Keys::glossaryEnabled) return QJsonValue(glossaryEnabled);
    if (key == Keys::glossaryEntries) return QJsonArray();
    if (key == Keys::promptSystemZh) return QJsonValue(promptSystemZh);
    if (key == Keys::promptSystemEn) return QJsonValue(promptSystemEn);
    if (key == Keys::promptReferenceZh) return QJsonValue(promptReferenceZh);
    if (key == Keys::promptReferenceEn) return QJsonValue(promptReferenceEn);
    if (key == Keys::promptToneZh) return QJsonValue(promptToneZh);
    if (key == Keys::promptToneEn) return QJsonValue(promptToneEn);
    if (key == Keys::promptStyleZh) return QJsonValue(promptStyleZh);
    if (key == Keys::promptStyleEn) return QJsonValue(promptStyleEn);
    if (key == Keys::promptBackgroundZh) return QJsonValue(promptBackgroundZh);
    if (key == Keys::promptBackgroundEn) return QJsonValue(promptBackgroundEn);
    if (key == Keys::promptGlossaryZh) return QJsonValue(promptGlossaryZh);
    if (key == Keys::promptGlossaryEn) return QJsonValue(promptGlossaryEn);
    if (key == Keys::promptDefaultZh) return QJsonValue(promptDefaultZh);
    if (key == Keys::promptDefaultEn) return QJsonValue(promptDefaultEn);
    if (key == Keys::promptCandidateZh) return QJsonValue(promptCandidateZh);
    if (key == Keys::promptCandidateEn) return QJsonValue(promptCandidateEn);
    if (key == Keys::clipboardMonitor) return QJsonValue(clipboardMonitor);
    if (key == Keys::clipboardDelayMs) return QJsonValue(clipboardDelayMs);
    if (key == Keys::historyEnabled) return QJsonValue(historyEnabled);
    if (key == Keys::historyMaxRecords) return QJsonValue(historyMaxRecords);
    return QJsonValue(QJsonValue::Undefined);
}

}
