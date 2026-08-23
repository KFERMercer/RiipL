#pragma once

#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace Prompts {

// Canonical prompt template identifiers from which per-language config keys derive.
inline const QString defaultTemplate = QStringLiteral("default");
inline const QString systemTemplate = QStringLiteral("system");
inline const QString glossaryTemplate = QStringLiteral("glossary");
inline const QString toneTemplate = QStringLiteral("tone");
inline const QString styleTemplate = QStringLiteral("style");
inline const QString backgroundTemplate = QStringLiteral("background");
inline const QString personalizationTemplate = QStringLiteral("personalization");
inline const QString candidateTemplate = QStringLiteral("candidate");

}

namespace Keys {

// Single construction point for per-language prompt template keys.
inline QString promptKey(const QString& name, const QString& language)
{
    return QStringLiteral("prompts.%1_%2").arg(name, language);
}

inline const QString promptDefaultZh = promptKey(Prompts::defaultTemplate, QStringLiteral("zh"));
inline const QString promptDefaultEn = promptKey(Prompts::defaultTemplate, QStringLiteral("en"));
inline const QString promptSystemZh = promptKey(Prompts::systemTemplate, QStringLiteral("zh"));
inline const QString promptSystemEn = promptKey(Prompts::systemTemplate, QStringLiteral("en"));
inline const QString promptGlossaryZh = promptKey(Prompts::glossaryTemplate, QStringLiteral("zh"));
inline const QString promptGlossaryEn = promptKey(Prompts::glossaryTemplate, QStringLiteral("en"));
inline const QString promptToneZh = promptKey(Prompts::toneTemplate, QStringLiteral("zh"));
inline const QString promptToneEn = promptKey(Prompts::toneTemplate, QStringLiteral("en"));
inline const QString promptStyleZh = promptKey(Prompts::styleTemplate, QStringLiteral("zh"));
inline const QString promptStyleEn = promptKey(Prompts::styleTemplate, QStringLiteral("en"));
inline const QString promptBackgroundZh = promptKey(Prompts::backgroundTemplate, QStringLiteral("zh"));
inline const QString promptBackgroundEn = promptKey(Prompts::backgroundTemplate, QStringLiteral("en"));
inline const QString promptPersonalizationZh = promptKey(Prompts::personalizationTemplate, QStringLiteral("zh"));
inline const QString promptPersonalizationEn = promptKey(Prompts::personalizationTemplate, QStringLiteral("en"));
inline const QString promptCandidateZh = promptKey(Prompts::candidateTemplate, QStringLiteral("zh"));
inline const QString promptCandidateEn = promptKey(Prompts::candidateTemplate, QStringLiteral("en"));

inline const QString apiBaseUrl = QStringLiteral("api.base_url");
inline const QString apiKey = QStringLiteral("api.api_key");
inline const QString apiModel = QStringLiteral("api.model");
inline const QString apiTemperature = QStringLiteral("api.temperature");
inline const QString apiMaxTokens = QStringLiteral("api.max_tokens");
inline const QString apiTopP = QStringLiteral("api.top_p");
inline const QString apiStream = QStringLiteral("api.stream");
inline const QString apiExtraBody = QStringLiteral("api.extra_body");

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
inline const QString translationPreferences = QStringLiteral("translation.preferences");

inline const QString glossaryEnabled = QStringLiteral("glossary.enabled");
inline const QString glossaryEntries = QStringLiteral("glossary.entries");

inline const QString hotkeyEnabled = QStringLiteral("hotkey.enabled");
inline const QString hotkeySequence = QStringLiteral("hotkey.sequence");

inline const QString clipboardMonitor = QStringLiteral("clipboard.monitor");
inline const QString clipboardDelayMs = QStringLiteral("clipboard.delay_ms");

inline const QString historyEnabled = QStringLiteral("history.enabled");
inline const QString historyMaxRecords = QStringLiteral("history.max_records");

}

namespace Defaults {

inline const QString apiBaseUrl = QStringLiteral("https://api.openai.com/v1");
inline const QString apiKey = QString();
inline const QString apiModel = QStringLiteral("gpt-4o-mini");
inline const double apiTemperature = 0.3;
inline const int apiMaxTokens = 4096;
inline const double apiTopP = 1.0;
inline const bool apiStream = true;
inline const QString apiExtraBody = QString();

inline const QString uiLanguage = QStringLiteral("auto");
inline const bool uiAutoTranslate = false;
inline const int uiAutoTranslateDelay = 800;
inline const QString uiWindowGeometry = QString();
inline const bool uiAlwaysOnTop = false;
inline const bool uiMinimizeToTray = false;
inline const int uiFontSize = 10;

inline const QString translationSourceLang = QStringLiteral("auto");
inline const QString translationTargetLang = QStringLiteral("en");
inline const QString translationTone = QStringLiteral("neutral");
inline const QString translationStyle = QString();
inline const QString translationBackground = QString();

inline const bool glossaryEnabled = true;

inline const bool hotkeyEnabled = false;
inline const QString hotkeySequence = QStringLiteral("Ctrl+Alt+T");

inline const bool clipboardMonitor = false;
inline const int clipboardDelayMs = 500;

inline const bool historyEnabled = true;
inline const int historyMaxRecords = 500;

inline const QString promptDefaultZh = R"TXT(将以下文本翻译为 {target_lang}，注意**只需要输出翻译后的结果，不要额外解释**：

{source_text})TXT";
inline const QString promptDefaultEn = R"TXT(Translate the following text into {target_lang}. Note that you should **only output the translated result without any additional explanation**:

{source_text})TXT";

inline const QString promptSystemZh = QStringLiteral("你是一位翻译专家。");
inline const QString promptSystemEn = QStringLiteral("You are a professional translator.");

inline const QString promptGlossaryZh = R"TXT(*参考下面的翻译：*
{glossary}
将以下文本翻译为 {target_lang}，注意**只需要输出翻译后的结果，不要额外解释**：

{source_text})TXT";
inline const QString promptGlossaryEn = R"TXT(*Reference the following translations:*
{glossary}
Translate the following text into {target_lang}. Note that you must **ONLY output the translated result without any additional explanation**:

{source_text})TXT";

inline const QString promptToneZh = R"TXT(请将以下文本翻译为 {target_lang}。
注意翻译的语气要严格符合【**{tone}**】

{source_text})TXT";
inline const QString promptToneEn = R"TXT(Please translate the following text into {target_lang}. Note that the translation tone must strictly conform to [**{tone}**]:

{source_text})TXT";

inline const QString promptStyleZh = R"TXT(请将以下文本翻译为 {target_lang}。
注意翻译的风格要严格符合【**{target_style}**】

{source_text})TXT";
inline const QString promptStyleEn = R"TXT(Please translate the following text into {target_lang}. Note that the translation style must strictly conform to [**{target_style}**]:

{source_text})TXT";

inline const QString promptBackgroundZh = R"TXT(*【背景信息】*
{background_text}

请结合背景信息将以下文本翻译为 {target_lang}。

*【待翻译文本】*
{source_text})TXT";
inline const QString promptBackgroundEn = R"TXT(*[Background Information]*
{background_text}

Please translate the following text into {target_lang}, taking the provided background information into consideration.

*[Source Text]*
{source_text})TXT";

inline const QString promptPersonalizationZh = R"TXT(*【待翻译文本】*
{source_text}

*【翻译任务】*
{user_preferences}
将【待翻译文本】翻译为 {target_lang}。)TXT";
inline const QString promptPersonalizationEn = R"TXT(*[Source Text]*
{source_text}

*[Translation Tasks]*
{user_preferences}
Translate the [Source Text] into {target_lang}.)TXT";



inline const QString promptCandidateZh = R"TXT(原文：{source_text}
译文：{translated_text}
用户在译文中选中了：「{word}」

请结合上下文判断选中内容对应的完整词语或短语（必要时可向左右扩展为更完整的词），
并提供 2-4 个可直接替换该词语的备选表达。

重要：replace 与所有 options 必须使用 {target_lang} 书写，与译文语言保持一致，
并保证替换回译文后语法通顺，禁止翻译成其他任何语言。

严格按以下 JSON 格式输出，禁止输出任何解释或代码块标记：
{{"replace": "译文中需要被替换的完整片段", "options": ["备选一", "备选二", "备选三"]}})TXT";
inline const QString promptCandidateEn = R"TXT(Source: {source_text}
Translation: {translated_text}
The user selected "{word}" in the translation.

Determine the complete word or phrase corresponding to the selection in context (expand to the left or right if needed),
then provide 2-4 alternative expressions that can directly replace it.

Important: "replace" and every entry in "options" MUST be written in {target_lang} — the same language as the translation —
and must fit grammatically when substituted back into it. Never use any other language.

Output strictly in the following JSON format with no explanation and no code fences:
{{"replace": "the exact fragment in the translation to be replaced", "options": ["option 1", "option 2", "option 3"]}})TXT";

inline QStringList allKeys()
{
    static const QStringList keys = {
        Keys::apiBaseUrl, Keys::apiKey, Keys::apiModel, Keys::apiTemperature,
        Keys::apiMaxTokens, Keys::apiTopP, Keys::apiStream, Keys::apiExtraBody,
        Keys::uiLanguage, Keys::uiAutoTranslate, Keys::uiAutoTranslateDelay,
        Keys::uiWindowGeometry, Keys::uiAlwaysOnTop, Keys::uiMinimizeToTray, Keys::uiFontSize,
        Keys::translationSourceLang, Keys::translationTargetLang, Keys::translationTone,
        Keys::translationCustomTones, Keys::translationStyle, Keys::translationBackground,
        Keys::translationPreferences,
        Keys::glossaryEnabled, Keys::glossaryEntries,
        Keys::promptDefaultZh, Keys::promptDefaultEn,
        Keys::promptSystemZh, Keys::promptSystemEn,
        Keys::promptGlossaryZh, Keys::promptGlossaryEn,
        Keys::promptToneZh, Keys::promptToneEn,
        Keys::promptStyleZh, Keys::promptStyleEn,
        Keys::promptBackgroundZh, Keys::promptBackgroundEn,
        Keys::promptPersonalizationZh, Keys::promptPersonalizationEn,
        Keys::promptCandidateZh, Keys::promptCandidateEn,
        Keys::hotkeyEnabled, Keys::hotkeySequence,
        Keys::clipboardMonitor, Keys::clipboardDelayMs,
        Keys::historyEnabled, Keys::historyMaxRecords
    };
    return keys;
}

inline QJsonValue value(const QString& key)
{
    if (key == Keys::apiBaseUrl) return QJsonValue(apiBaseUrl);
    if (key == Keys::apiKey) return QJsonValue(apiKey);
    if (key == Keys::apiModel) return QJsonValue(apiModel);
    if (key == Keys::apiTemperature) return QJsonValue(apiTemperature);
    if (key == Keys::apiMaxTokens) return QJsonValue(apiMaxTokens);
    if (key == Keys::apiTopP) return QJsonValue(apiTopP);
    if (key == Keys::apiStream) return QJsonValue(apiStream);
    if (key == Keys::apiExtraBody) return QJsonValue(apiExtraBody);
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
    if (key == Keys::translationPreferences) return QJsonArray();
    if (key == Keys::glossaryEnabled) return QJsonValue(glossaryEnabled);
    if (key == Keys::glossaryEntries) return QJsonArray();
    if (key == Keys::promptDefaultZh) return QJsonValue(promptDefaultZh);
    if (key == Keys::promptDefaultEn) return QJsonValue(promptDefaultEn);
    if (key == Keys::promptSystemZh) return QJsonValue(promptSystemZh);
    if (key == Keys::promptSystemEn) return QJsonValue(promptSystemEn);
    if (key == Keys::promptGlossaryZh) return QJsonValue(promptGlossaryZh);
    if (key == Keys::promptGlossaryEn) return QJsonValue(promptGlossaryEn);
    if (key == Keys::promptToneZh) return QJsonValue(promptToneZh);
    if (key == Keys::promptToneEn) return QJsonValue(promptToneEn);
    if (key == Keys::promptStyleZh) return QJsonValue(promptStyleZh);
    if (key == Keys::promptStyleEn) return QJsonValue(promptStyleEn);
    if (key == Keys::promptBackgroundZh) return QJsonValue(promptBackgroundZh);
    if (key == Keys::promptBackgroundEn) return QJsonValue(promptBackgroundEn);
    if (key == Keys::promptPersonalizationZh) return QJsonValue(promptPersonalizationZh);
    if (key == Keys::promptPersonalizationEn) return QJsonValue(promptPersonalizationEn);
    if (key == Keys::promptCandidateZh) return QJsonValue(promptCandidateZh);
    if (key == Keys::promptCandidateEn) return QJsonValue(promptCandidateEn);
    if (key == Keys::hotkeyEnabled) return QJsonValue(hotkeyEnabled);
    if (key == Keys::hotkeySequence) return QJsonValue(hotkeySequence);
    if (key == Keys::clipboardMonitor) return QJsonValue(clipboardMonitor);
    if (key == Keys::clipboardDelayMs) return QJsonValue(clipboardDelayMs);
    if (key == Keys::historyEnabled) return QJsonValue(historyEnabled);
    if (key == Keys::historyMaxRecords) return QJsonValue(historyMaxRecords);
    return QJsonValue(QJsonValue::Undefined);
}

}
