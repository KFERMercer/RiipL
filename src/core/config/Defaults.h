#pragma once

#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace CandidateMarks {

// Bracket pair wrapped around the selection before a fragment reaches the model.
// A paired glyph is used because single-character markers get echoed back
// inside `old`.
inline const QString selectionOpen = QStringLiteral("[[");
inline const QString selectionClose = QStringLiteral("]]");

}

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
// Wording request for a translation short enough to sit inside the context
// window, which lets it carry the source text as well.
inline const QString candidateShortTemplate = QStringLiteral("candidate_short");

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
inline const QString promptCandidateShortZh = promptKey(Prompts::candidateShortTemplate, QStringLiteral("zh"));
inline const QString promptCandidateShortEn = promptKey(Prompts::candidateShortTemplate, QStringLiteral("en"));

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
// Offered interface languages, in the order the pickers list them.
inline const QStringList& uiLanguageCodes()
{
    static const QStringList codes = {
        QStringLiteral("en"),
        QStringLiteral("zh")
    };
    return codes;
}
// Follows the session locale instead of naming a language.
inline const QString uiLanguageAuto = QStringLiteral("auto");
inline bool isOfferedUiLanguage(const QString& code)
{
    return code == uiLanguageAuto || uiLanguageCodes().contains(code);
}
inline const QString uiAutoTranslate = QStringLiteral("ui.auto_translate");
inline const QString uiAutoTranslateDelay = QStringLiteral("ui.auto_translate_delay");
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
inline const bool uiAlwaysOnTop = false;
inline const bool uiMinimizeToTray = true;
inline const int uiFontSize = 10;

inline const QString translationSourceLang = QStringLiteral("auto");
inline const QString translationTargetLang = QStringLiteral("zh");
inline const QString translationTone = QStringLiteral("default");
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
inline const QString promptReferenceZh = R"TXT(你需要仔细阅读并严格遵守以下约束：
)TXT";
inline const QString promptReferenceEn = R"TXT(Read the following reference information carefully and follow it strictly:
)TXT";

inline const QString promptToneZh = R"TXT(- 翻译语气：{tone}
)TXT";
inline const QString promptToneEn = R"TXT(- Translation tone: {tone}
)TXT";

inline const QString promptStyleZh = R"TXT(- 语言风格：{style}
)TXT";
inline const QString promptStyleEn = R"TXT(- Language style: {style}
)TXT";

inline const QString promptBackgroundZh = R"TXT(- 背景信息：
  ```
  {background}
  ```
)TXT";
inline const QString promptBackgroundEn = R"TXT(- Background Information:
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
{source_text})TXT";
inline const QString promptDefaultEn = R"TXT(Based on the reference information above, translate the following text into {target_lang}. Note that you must **only output the translated result without any additional explanation**:
{source_text})TXT";

inline const QString promptCandidateZh = R"TXT(你的任务是寻找给定原文中被标记的词的替代遣词或表述，并返回 JSON 结构化方案。

原文：

```
{selected_fragment}
```

被标记的词为：`{selected_word}`（已用标示符 `{mark_left}` 和 `{mark_right}` 标出，这两处标示符不属于原文)。

处理步骤：
1. 在原文中定位 {mark_left} 与 {mark_right} 之间的内容。
2. 在句中找出包含 `{selected_word}` 的**最小完整词语或固定搭配**，作为 `old`。
3. 为 `old` 写出 2 到 4 条可直接替换的表达，放进 `new` 数组。

约束：
- `old` 必须从原文中直接复制，逐字节一致，包括大小写与标点；不得改写、拼接或虚构。
- `old` 必须包含 `{selected_word}`；如果 `{selected_word}` 只是某个词的一部分，`old` 必须扩展到该完整词语。
- `old` 不得缩小为 `{selected_word}` 本身，除非标记内容在句中确实是一个独立完整的词。
- `old` 中不得出现 {mark_left} 和 {mark_right}。
- `new` 中每条表达均与 `old` 不同，且彼此互不相同。
- `new` 一律使用 {target_lang} 书写，不得混入其他语言。
- `new` 是同一语言内的近义改写，不是翻译，禁止译成其他语言。
- 用 `new` 替换 `old` 后，整句必须通顺、意思不变，且结果中不得出现相邻重复的字。
- 替换范围以 `old` 为准，句中其他部分保持原样。

只输出如下 JSON 数组，不要解释、不要代码块标记，每个需要替换的片段对应一个对象：
[{"old":"原文片段1","new":["替换表达1","替换表达2"]},{"old":"原文片段2","new":["替换表达3","替换表达4"]}])TXT";
inline const QString promptCandidateEn = R"TXT(Your task is to find alternative wordings for the marked word in the given source text, and return a structured JSON plan.

Source text:

```
{selected_fragment}
```

The marked word is: `{selected_word}` (marked with the markers `{mark_left}` and `{mark_right}`; those two markers are not part of the source text.)

Steps:
1. Locate the content between {mark_left} and {mark_right} in the source text.
2. Find the **smallest complete word or set phrase** in the sentence that contains `{selected_word}`, and use it as `old`.
3. Write 2 to 4 expressions that can directly replace `old` and put them into the `new` array.

Constraints:
- `old` must be copied verbatim from the source text, character for character, including case and punctuation; do not rewrite, splice or invent it.
- `old` must contain `{selected_word}`; if `{selected_word}` is only part of a word, `old` must widen to that complete word.
- `old` must not shrink to `{selected_word}` itself unless the marked content really is a complete standalone word in the sentence.
- `old` must not contain {mark_left} or {mark_right}.
- Every entry in `new` differs from `old` and from the other entries.
- `new` entries must be written in {target_lang} only, without mixing in another language.
- `new` entries are paraphrases within the same language, not translations.
- Replacing `old` with a `new` entry must leave the whole sentence reading correctly and keep its meaning, with no adjacent repeated characters in the result.
- The replacement range is exactly `old`; leave the rest of the sentence untouched.

Output only the following JSON array, with no explanation and no code fences, one object per fragment to replace:
[{"old":"source fragment 1","new":["alternative 1","alternative 2"]},{"old":"source fragment 2","new":["alternative 3","alternative 4"]}])TXT";

inline const QString promptCandidateShortZh = R"TXT(你的任务是寻找译文中被标记的词的替代遣词或表述，并返回 JSON 结构化方案。

译文：

```
{selected_fragment}
```

翻译前的原文：

```
{source_text}
```

被标记的词为：`{selected_word}` （已用标示符 `{mark_left}` 和 `{mark_right}` 标出)。

处理步骤：
1. 在译文中定位 {mark_left} 与 {mark_right} 之间的内容。
2. 在句中找出包含 `{selected_word}` 的**最小完整词语或固定搭配**，作为 `old`。
3. 参考翻译前的原文，为 `old` 写出 2 到 4 条可直接替换的表达，放进 `new` 数组。

约束：
- `old` 必须从译文中直接复制，逐字节一致，包括大小写与标点；不得改写、拼接或虚构。
- `old` 必须包含 `{selected_word}`；如果 `{selected_word}` 只是某个词的一部分，`old` 必须扩展到该完整词语。
- `old` 不得缩小为 `{selected_word}` 本身，除非标记内容在句中确实是一个独立完整的词。
- `old` 中不得出现 {mark_left} 和 {mark_right}。
- `new` 中每条表达均与 `old` 不同，且彼此互不相同。
- `new` 一律使用 {target_lang} 书写，不得混入其他语言。
- `new` 是同一语言内的近义改写，不是翻译，禁止译成其他语言。
- 用 `new` 替换 `old` 后，整句必须通顺、意思不变，且结果中不得出现相邻重复的字。
- 替换范围以 `old` 为准，句中其他部分保持原样。

只输出如下 JSON 数组，不要解释、不要代码块标记，每个需要替换的片段对应一个对象：
[{"old":"译文片段1","new":["替换表达1","替换表达2"]},{"old":"译文片段12","new":["替换表达3","替换表达4"]}])TXT";
inline const QString promptCandidateShortEn = R"TXT(Your task is to find alternative wordings for the marked word in the translation, and return a structured JSON plan.

Translation:

```
{selected_fragment}
```

Source text before translation:

```
{source_text}
```

The marked word is: `{selected_word}` (marked with the markers `{mark_left}` and `{mark_right}`.)

Steps:
1. Locate the content between {mark_left} and {mark_right} in the translation.
2. Find the **smallest complete word or set phrase** in the sentence that contains `{selected_word}`, and use it as `old`.
3. Consult the source text before translation and write 2 to 4 expressions that can directly replace `old`, then put them into the `new` array.

Constraints:
- `old` must be copied verbatim from the translation, character for character, including case and punctuation; do not rewrite, splice or invent it.
- `old` must contain `{selected_word}`; if `{selected_word}` is only part of a word, `old` must widen to that complete word.
- `old` must not shrink to `{selected_word}` itself unless the marked content really is a complete standalone word in the sentence.
- `old` must not contain {mark_left} or {mark_right}.
- Every entry in `new` differs from `old` and from the other entries.
- `new` entries must be written in {target_lang} only, without mixing in another language.
- `new` entries are paraphrases within the same language, not translations.
- Replacing `old` with a `new` entry must leave the whole sentence reading correctly and keep its meaning, with no adjacent repeated characters in the result.
- The replacement range is exactly `old`; leave the rest of the sentence untouched.

Output only the following JSON array, with no explanation and no code fences, one object per fragment to replace:
[{"old":"translated fragment 1","new":["alternative 1","alternative 2"]},{"old":"translated fragment 2","new":["alternative 3","alternative 4"]}])TXT";

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
    if (key == Keys::promptCandidateShortZh) return QJsonValue(promptCandidateShortZh);
    if (key == Keys::promptCandidateShortEn) return QJsonValue(promptCandidateShortEn);
    if (key == Keys::clipboardMonitor) return QJsonValue(clipboardMonitor);
    if (key == Keys::clipboardDelayMs) return QJsonValue(clipboardDelayMs);
    if (key == Keys::historyEnabled) return QJsonValue(historyEnabled);
    if (key == Keys::historyMaxRecords) return QJsonValue(historyMaxRecords);
    return QJsonValue(QJsonValue::Undefined);
}

}
