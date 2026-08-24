#include "PromptBuilder.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/translation/Language.h"

QString PromptBuilder::templateFor(const QString& name, const QString& uiLanguage)
{
    ConfigManager* config = ConfigManager::instance();
    const QString value = config->stringValue(Keys::promptKey(name, uiLanguage));
    if (!value.isEmpty())
        return value;
    const QString fallbackLanguage = uiLanguage == QLatin1String("zh") ? QStringLiteral("en") : QStringLiteral("zh");
    const QString fallback = config->stringValue(Keys::promptKey(name, fallbackLanguage));
    if (!fallback.isEmpty())
        return fallback;
    return QString();
}

QStringList PromptBuilder::glossaryLines(const QVector<GlossaryEntry>& entries)
{
    QStringList lines;
    lines.reserve(entries.size());
    for (const GlossaryEntry& entry : entries) {
        if (entry.source.isEmpty())
            continue;
        if (entry.target.isEmpty())
            lines << QStringLiteral("%1 (leave untranslated)").arg(entry.source);
        else
            lines << QStringLiteral("%1 translates to %2").arg(entry.source, entry.target);
    }
    return lines;
}

QStringList PromptBuilder::knownPlaceholders()
{
    return {
        QStringLiteral("source_text"),
        QStringLiteral("target_lang"),
        QStringLiteral("source_lang"),
        QStringLiteral("tone"),
        QStringLiteral("target_style"),
        QStringLiteral("background_text"),
        QStringLiteral("glossary"),
        QStringLiteral("user_preferences"),
        QStringLiteral("word"),
        QStringLiteral("translated_text")
    };
}

QString PromptBuilder::substitute(QString text, const QHash<QString, QString>& variables)
{
    for (auto it = variables.constBegin(); it != variables.constEnd(); ++it) {
        text.replace(QLatin1Char('{') + it.key() + QLatin1Char('}'), it.value());
    }
    return text;
}

PromptBuilder::Result PromptBuilder::build(const TranslationContext& context)
{
    QHash<QString, QString> variables;
    variables.insert(QStringLiteral("source_text"), context.sourceText);
    variables.insert(QStringLiteral("target_lang"), Languages::englishName(context.targetLang));
    variables.insert(QStringLiteral("source_lang"), Languages::englishName(context.sourceLang));
    variables.insert(QStringLiteral("tone"), context.tone);
    variables.insert(QStringLiteral("target_style"), context.style);
    variables.insert(QStringLiteral("background_text"), context.background);
    variables.insert(QStringLiteral("glossary"), glossaryLines(context.glossary).join(QLatin1Char('\n')));
    QStringList numbered;
    numbered.reserve(context.preferences.size());
    for (int i = 0; i < context.preferences.size(); ++i)
        numbered << QStringLiteral("%1. **%2**").arg(i + 1).arg(context.preferences.at(i));
    variables.insert(QStringLiteral("user_preferences"), numbered.join(QLatin1Char('\n')));
    variables.insert(QStringLiteral("word"), QString());
    variables.insert(QStringLiteral("translated_text"), QString());

    QStringList fragments;

    if (!context.background.isEmpty())
        fragments << templateFor(Prompts::backgroundTemplate, context.uiLanguage);
    if (context.glossaryEnabled && !context.glossary.isEmpty())
        fragments << templateFor(Prompts::glossaryTemplate, context.uiLanguage);
    if (!context.tone.isEmpty() && context.tone != QLatin1String("neutral"))
        fragments << templateFor(Prompts::toneTemplate, context.uiLanguage);
    if (!context.style.isEmpty())
        fragments << templateFor(Prompts::styleTemplate, context.uiLanguage);
    if (!context.preferences.isEmpty())
        fragments << templateFor(Prompts::personalizationTemplate, context.uiLanguage);
    fragments << templateFor(Prompts::defaultTemplate, context.uiLanguage);

    Result result;
    result.system = substitute(templateFor(Prompts::systemTemplate, context.uiLanguage), variables);
    for (const QString& fragment : std::as_const(fragments)) {
        if (fragment.isEmpty())
            continue;
        if (!result.user.isEmpty())
            result.user += QStringLiteral("\n\n");
        result.user += substitute(fragment, variables);
    }
    return result;
}

QString PromptBuilder::candidatePrompt(const QString& sourceText,
                                       const QString& translatedText,
                                       const QString& word,
                                       const QString& targetLang,
                                       const QString& uiLanguage)
{
    QHash<QString, QString> variables;
    variables.insert(QStringLiteral("source_text"), sourceText);
    variables.insert(QStringLiteral("translated_text"), translatedText);
    variables.insert(QStringLiteral("word"), word);
    variables.insert(QStringLiteral("target_lang"), Languages::englishName(targetLang));
    const QString templ = templateFor(Prompts::candidateTemplate, uiLanguage);
    return substitute(templ, variables);
}
