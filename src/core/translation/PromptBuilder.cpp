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

QString PromptBuilder::referenceEntry(const QString& label, const QString& body, const QString& fenceLanguage)
{
    if (body.isEmpty())
        return QString();
    QStringList lines;
    if (!label.isEmpty())
        lines << QStringLiteral("- ") + label;
    lines << QStringLiteral("  ```") + fenceLanguage;
    const QStringList bodyLines = body.split(QLatin1Char('\n'));
    lines.reserve(lines.size() + bodyLines.size() + 1);
    for (const QString& line : bodyLines)
        lines << QStringLiteral("  ") + line;
    lines << QStringLiteral("  ```");
    return lines.join(QLatin1Char('\n'));
}

QString PromptBuilder::referenceBlock(const QStringList& entries, const QString& uiLanguage)
{
    QStringList blocks;
    for (const QString& entry : entries) {
        if (!entry.isEmpty())
            blocks << entry;
    }
    if (blocks.isEmpty())
        return QString();
    const QString header = templateFor(Prompts::referenceTemplate, uiLanguage);
    const QString body = blocks.join(QStringLiteral("\n"));
    return header.isEmpty() ? body : header + QStringLiteral("\n\n") + body;
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
        text.replace(QLatin1Char('{') + it.key() + QLatin1Char('}'), it.value());
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

    const auto labelFor = [&variables, &uiLanguage](const QString& name) {
        return substitute(templateFor(name, uiLanguage), variables);
    };

    // Each reference entry is appended only when its value is set; an unset
    // option contributes nothing, so it cannot bias the translation.
    QStringList entries;
    if (!context.tone.isEmpty() && context.tone != QLatin1String("neutral"))
        entries << referenceEntry(labelFor(Prompts::toneTemplate), context.tone);
    if (!context.style.isEmpty())
        entries << referenceEntry(labelFor(Prompts::styleTemplate), context.style);
    if (!context.background.isEmpty())
        entries << referenceEntry(labelFor(Prompts::backgroundTemplate), context.background);
    if (!glossary.isEmpty())
        entries << referenceEntry(labelFor(Prompts::glossaryTemplate), glossary, QStringLiteral("json"));

    QStringList fragments;
    const QString reference = referenceBlock(entries, uiLanguage);
    if (!reference.isEmpty())
        fragments << reference;
    const QString instruction = substitute(templateFor(Prompts::defaultTemplate, uiLanguage), variables);
    if (!instruction.isEmpty())
        fragments << instruction;

    Result result;
    result.system = substitute(templateFor(Prompts::systemTemplate, uiLanguage), variables);
    result.user = fragments.join(QStringLiteral("\n\n"));
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
