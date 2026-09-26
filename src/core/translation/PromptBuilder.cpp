#include "PromptBuilder.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/translation/Language.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// Localized keys of the rendered glossary; they follow the UI language so the
// data block reads in the same language as the template that labels it.
const QString& sourceKey(const QString& uiLanguage)
{
    static const QString zh = QStringLiteral("原文");
    static const QString en = QStringLiteral("source");
    return uiLanguage == QLatin1String("zh") ? zh : en;
}

const QString& targetKey(const QString& uiLanguage)
{
    static const QString zh = QStringLiteral("译文");
    static const QString en = QStringLiteral("target");
    return uiLanguage == QLatin1String("zh") ? zh : en;
}

}

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

QString PromptBuilder::glossaryData(const QVector<GlossaryEntry>& entries, const QString& uiLanguage)
{
    QJsonArray array;
    for (const GlossaryEntry& entry : entries) {
        const QString source = entry.source.trimmed();
        if (source.isEmpty())
            continue;
        QJsonObject object;
        object.insert(sourceKey(uiLanguage), source);
        // An entry without a target keeps the source term untouched. Emitting
        // the source as its own target states that directly, whereas a null
        // value is read as a literal "null" by some models.
        object.insert(targetKey(uiLanguage), entry.target.trimmed().isEmpty() ? source : entry.target.trimmed());
        array.append(object);
    }
    if (array.isEmpty())
        return QString();
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Indented)).trimmed();
}

QStringList PromptBuilder::knownPlaceholders()
{
    return {
        QStringLiteral("source_lang"),
        QStringLiteral("target_lang"),
        QStringLiteral("tone"),
        QStringLiteral("style"),
        QStringLiteral("background"),
        QStringLiteral("glossary"),
        QStringLiteral("source_text"),
        QStringLiteral("translated_text"),
        QStringLiteral("selected_word")
    };
}

QString PromptBuilder::substitute(QString text, const QHash<QString, QString>& variables)
{
    for (auto it = variables.constBegin(); it != variables.constEnd(); ++it) {
        const QString token = QLatin1Char('{') + it.key() + QLatin1Char('}');
        const QString& value = it.value();
        if (!value.contains(QLatin1Char('\n'))) {
            text.replace(token, value);
            continue;
        }
        // A multi-line value inherits the indentation of the line holding its
        // placeholder, so a template can indent one line and keep every line of
        // the value aligned under it. Occurrences are rewritten back to front so
        // earlier positions stay valid, and each one uses its own indentation.
        int at = text.lastIndexOf(token);
        while (at >= 0) {
            int lineStart = text.lastIndexOf(QLatin1Char('\n'), at) + 1;
            QString indent;
            while (lineStart < at && text.at(lineStart).isSpace()) {
                indent += text.at(lineStart);
                ++lineStart;
            }
            QString replacement = value;
            if (!indent.isEmpty())
                replacement.replace(QLatin1Char('\n'), QLatin1Char('\n') + indent);
            text.replace(at, token.size(), replacement);
            at = text.lastIndexOf(token, at - 1);
        }
    }
    return text;
}

PromptBuilder::Result PromptBuilder::build(const TranslationContext& context)
{
    const QString uiLanguage = context.uiLanguage;
    const QString glossary = context.glossaryEnabled
        ? glossaryData(context.glossary, uiLanguage)
        : QString();

    QHash<QString, QString> variables;
    variables.insert(QStringLiteral("source_lang"), Languages::englishName(context.sourceLang));
    variables.insert(QStringLiteral("target_lang"), Languages::englishName(context.targetLang));
    variables.insert(QStringLiteral("tone"), context.tone);
    variables.insert(QStringLiteral("style"), context.style);
    variables.insert(QStringLiteral("background"), context.background);
    variables.insert(QStringLiteral("glossary"), glossary);
    variables.insert(QStringLiteral("source_text"), context.sourceText);
    variables.insert(QStringLiteral("translated_text"), QString());
    variables.insert(QStringLiteral("selected_word"), QString());

    const auto render = [&variables, &uiLanguage](const QString& name) {
        return substitute(templateFor(name, uiLanguage), variables);
    };

    // Reference entries appear in the order they are listed here, and only
    // while their variable holds a value, so unset options stay out entirely.
    QStringList referenceEntries;
    if (!context.tone.isEmpty() && context.tone != QLatin1String("neutral"))
        referenceEntries << render(Prompts::toneTemplate);
    if (!context.style.isEmpty())
        referenceEntries << render(Prompts::styleTemplate);
    if (!context.background.isEmpty())
        referenceEntries << render(Prompts::backgroundTemplate);
    if (!glossary.isEmpty())
        referenceEntries << render(Prompts::glossaryTemplate);

    QStringList fragments;
    if (!referenceEntries.isEmpty()) {
        const QString header = render(Prompts::referenceTemplate).trimmed();
        fragments << (header.isEmpty() ? referenceEntries.join(QString())
                                       : header + QStringLiteral("\n\n") + referenceEntries.join(QString()));
    }
    fragments << render(Prompts::defaultTemplate);

    QStringList parts;
    for (const QString& fragment : fragments) {
        const QString trimmed = fragment.trimmed();
        if (!trimmed.isEmpty())
            parts << trimmed;
    }

    Result result;
    result.system = render(Prompts::systemTemplate).trimmed();
    result.user = parts.join(QStringLiteral("\n\n"));
    return result;
}

QString PromptBuilder::candidatePrompt(const QString& sourceText,
                                       const QString& translatedText,
                                       const QString& word,
                                       const QString& targetLang,
                                       const QString& uiLanguage)
{
    QHash<QString, QString> variables;
    variables.insert(QStringLiteral("target_lang"), Languages::englishName(targetLang));
    variables.insert(QStringLiteral("source_text"), sourceText);
    variables.insert(QStringLiteral("translated_text"), translatedText);
    variables.insert(QStringLiteral("selected_word"), word);
    const QString templ = templateFor(Prompts::candidateTemplate, uiLanguage);
    return substitute(templ, variables);
}
