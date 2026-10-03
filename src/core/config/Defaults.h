#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace Keys {

inline const QString promptSystem = QStringLiteral("prompts.system");
inline const QString promptReference = QStringLiteral("prompts.reference");
inline const QString promptTone = QStringLiteral("prompts.tone");
inline const QString promptStyle = QStringLiteral("prompts.style");
inline const QString promptBackground = QStringLiteral("prompts.background");
inline const QString promptGlossary = QStringLiteral("prompts.glossary");
inline const QString promptDefault = QStringLiteral("prompts.default");
inline const QString promptDocument = QStringLiteral("prompts.document");
inline const QString promptCandidate = QStringLiteral("prompts.candidate");
inline const QString promptCandidateShort = QStringLiteral("prompts.candidate_short");

inline const QString apiBaseUrl = QStringLiteral("api.base_url");
inline const QString apiKey = QStringLiteral("api.api_key");
inline const QString apiModel = QStringLiteral("api.model");
inline const QString apiTimeoutMs = QStringLiteral("api.timeout_ms");
inline const QString apiTemperature = QStringLiteral("api.temperature");
inline const QString apiMaxTokens = QStringLiteral("api.max_tokens");
inline const QString apiStream = QStringLiteral("api.stream");
inline const QString apiExtraBody = QStringLiteral("api.extra_body");
inline const QString apiCustomHeaders = QStringLiteral("api.custom_headers");
inline const QString apiMaxConcurrency = QStringLiteral("api.max_concurrency");
inline const QString apiPresets = QStringLiteral("api.presets");

// Fields a named API preset captures, in settings-page order.
inline const QStringList& apiPresetFields()
{
    static const QStringList fields = {
        apiBaseUrl, apiKey, apiModel, apiTimeoutMs, apiTemperature,
        apiMaxTokens, apiStream, apiMaxConcurrency, apiExtraBody, apiCustomHeaders,
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

inline const QString documentWindowCharacters = QStringLiteral("document.window_characters");
inline const QString documentWindowLines = QStringLiteral("document.window_lines");
inline const QString documentRetryCount = QStringLiteral("document.retry_count");
inline const QString documentConcurrent = QStringLiteral("document.concurrent");
inline const QString documentCacheEnabled = QStringLiteral("document.cache_enabled");

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
inline const double apiTemperature = 0.0;
inline const int apiMaxTokens = 4096;
inline const int apiMaxConcurrency = 1;
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

inline const int documentWindowCharacters = 500;
inline const int documentWindowLines = 10;
inline const int documentRetryCount = 3;
inline const bool documentConcurrent = true;
inline const bool documentCacheEnabled = true;

inline const bool glossaryEnabled = false;

inline const bool clipboardMonitor = false;
inline const int clipboardDelayMs = 500;

inline const bool historyEnabled = true;
inline const int historyMaxRecords = 500;

inline const QString promptSystem = QStringLiteral("You are a professional translator.");

// A reference template renders its own label, fence and placeholder, and is
// emitted only while its variable holds a value.
inline const QString promptReference = R"TXT(Read the following reference information carefully and follow it strictly:
)TXT";

inline const QString promptTone = R"TXT(- Translation tone: {tone}
)TXT";

inline const QString promptStyle = R"TXT(- Language style: {style}
)TXT";

inline const QString promptBackground = R"TXT(- Background Information:
  ```
  {background}
  ```
)TXT";

inline const QString promptGlossary = R"TXT(- Glossary:
  ```json
  {glossary}
  ```
)TXT";

inline const QString promptDefault = R"TXT(Based on the reference information above, translate the following text into {target_lang}. Note that you must **only output the translated result without any additional explanation**:
{source_text})TXT";

inline const QString promptDocument = R"TXT(The segment to translate follows as a JSON object. Every key is a line number
and every value is the text of that line:

```json
{window}
```

Behaviour constraints, which must be followed strictly:

1. The object holds {window_lines} keys, one per line, numbered "1" to "{window_lines}"; answer with exactly {window_lines} keys in the same order.
2. Each key describes one line of the document: never merge, split, add or drop a key, and never answer with an empty value.
3. The context segments below are provided for understanding only; never translate them. Their line breaks carry no meaning.

Context segments, for understanding only:

- Previous segment:
  ```
  {prev_segment}
  ```
- Next segment:
  ```
  {next_segment}
  ```

Translate every value of the object into {target_lang}. Note that you must **only output the translated result without any additional explanation**, as a JSON object with exactly {window_lines} keys numbered "1" to "{window_lines}".)TXT";

inline const QString promptCandidate = R"TXT(Your task is to find alternative wordings for the marked word in the given original text, and return a structured JSON plan.

Original text:

```
{selected_fragment}
```

The marked word is: `{selected_word}` (marked with the markers `{mark_left}` and `{mark_right}`; those markers are not part of the original text.)

Steps:
1. Locate the content between {mark_left} and {mark_right} in the original text.
2. Find the **smallest complete word or set phrase** in the sentence that contains `{selected_word}`, and use it as `old`.
3. Write 2 to 4 expressions that can directly replace `old` and put them into the `new` array.

Constraints:
- `old` must be copied verbatim from the original text, character for character, including case and punctuation; do not rewrite, splice or invent it.
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

inline const QString promptCandidateShort = R"TXT(Your task is to find alternative wordings for the marked word in the translation, and return a structured JSON plan.

Translation:

```
{selected_fragment}
```

Source text before translation:

```
{source_text}
```

The marked word is: `{selected_word}` (marked with the markers `{mark_left}` and `{mark_right}`; those markers are not part of the original text.)

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
    static const QHash<QString, QJsonValue> defaults = {
        {Keys::apiBaseUrl, apiBaseUrl},
        {Keys::apiKey, apiKey},
        {Keys::apiModel, apiModel},
        {Keys::apiTimeoutMs, apiTimeoutMs},
        {Keys::apiTemperature, apiTemperature},
        {Keys::apiMaxTokens, apiMaxTokens},
        {Keys::apiMaxConcurrency, apiMaxConcurrency},
        {Keys::apiStream, apiStream},
        {Keys::apiExtraBody, apiExtraBody},
        {Keys::apiCustomHeaders, apiCustomHeaders},
        {Keys::apiPresets, QJsonArray()},
        {Keys::uiLanguage, uiLanguage},
        {Keys::uiAutoTranslate, uiAutoTranslate},
        {Keys::uiAutoTranslateDelay, uiAutoTranslateDelay},
        {Keys::uiAlwaysOnTop, uiAlwaysOnTop},
        {Keys::uiMinimizeToTray, uiMinimizeToTray},
        {Keys::uiFontSize, uiFontSize},
        {Keys::translationSourceLang, translationSourceLang},
        {Keys::translationTargetLang, translationTargetLang},
        {Keys::translationTone, translationTone},
        {Keys::translationCustomTones, QJsonArray()},
        {Keys::translationStyle, translationStyle},
        {Keys::translationBackground, translationBackground},
        {Keys::documentWindowCharacters, documentWindowCharacters},
        {Keys::documentWindowLines, documentWindowLines},
        {Keys::documentRetryCount, documentRetryCount},
        {Keys::documentConcurrent, documentConcurrent},
        {Keys::documentCacheEnabled, documentCacheEnabled},
        {Keys::glossaryEnabled, glossaryEnabled},
        {Keys::glossaryEntries, QJsonArray()},
        {Keys::promptSystem, promptSystem},
        {Keys::promptReference, promptReference},
        {Keys::promptTone, promptTone},
        {Keys::promptStyle, promptStyle},
        {Keys::promptBackground, promptBackground},
        {Keys::promptGlossary, promptGlossary},
        {Keys::promptDefault, promptDefault},
        {Keys::promptDocument, promptDocument},
        {Keys::promptCandidate, promptCandidate},
        {Keys::promptCandidateShort, promptCandidateShort},
        {Keys::clipboardMonitor, clipboardMonitor},
        {Keys::clipboardDelayMs, clipboardDelayMs},
        {Keys::historyEnabled, historyEnabled},
        {Keys::historyMaxRecords, historyMaxRecords},
    };
    return defaults.value(key, QJsonValue(QJsonValue::Undefined));
}

}
