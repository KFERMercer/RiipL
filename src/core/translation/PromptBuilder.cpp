#include "PromptBuilder.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/translation/Language.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

const QString kGlossarySourceKey = QStringLiteral("source");
const QString kGlossaryTargetKey = QStringLiteral("target");

}

QString PromptBuilder::glossaryData(const QVector<GlossaryEntry>& entries)
{
    QJsonArray array;
    for (const GlossaryEntry& entry : entries) {
        const QString source = entry.source.trimmed();
        if (source.isEmpty())
            continue;
        QJsonObject object;
        object.insert(kGlossarySourceKey, source);
        // A term without a target repeats its source, which states "keep as is";
        // a null would be read as a literal "null" by some models.
        object.insert(kGlossaryTargetKey, entry.target.trimmed().isEmpty() ? source : entry.target.trimmed());
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
        QStringLiteral("selected_fragment"),
        QStringLiteral("selected_word"),
        QStringLiteral("mark_left"),
        QStringLiteral("mark_right")
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
        // placeholder. Occurrences are rewritten back to front so earlier
        // positions stay valid.
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

QHash<QString, QString> PromptBuilder::variablesFor(const TranslationContext& context)
{
    QHash<QString, QString> variables;
    variables.insert(QStringLiteral("source_lang"), Languages::englishName(context.sourceLang));
    variables.insert(QStringLiteral("target_lang"), Languages::englishName(context.targetLang));
    variables.insert(QStringLiteral("tone"), context.tone);
    variables.insert(QStringLiteral("style"), context.style);
    variables.insert(QStringLiteral("background"), context.background);
    variables.insert(QStringLiteral("glossary"),
                     context.glossaryEnabled ? glossaryData(context.glossary) : QString());
    variables.insert(QStringLiteral("source_text"), context.sourceText);
    variables.insert(QStringLiteral("translated_text"), context.translatedText);
    variables.insert(QStringLiteral("selected_fragment"), context.selectedFragment);
    variables.insert(QStringLiteral("selected_word"), context.selectedWord);
    variables.insert(QStringLiteral("mark_left"), CandidateMarks::selectionOpen);
    variables.insert(QStringLiteral("mark_right"), CandidateMarks::selectionClose);
    return variables;
}

QString PromptBuilder::render(const QString& key, const QHash<QString, QString>& variables)
{
    return substitute(ConfigManager::instance()->stringValue(key), variables);
}

PromptBuilder::Result PromptBuilder::build(const TranslationContext& context)
{
    const QHash<QString, QString> variables = variablesFor(context);
    const QString glossary = variables.value(QStringLiteral("glossary"));

    // Reference entries appear in the order listed here and only while their
    // variable holds a value. The default tone is the absence of a tone, so it
    // is skipped like an empty one.
    QStringList referenceEntries;
    if (!context.tone.isEmpty() && context.tone != QLatin1String("default"))
        referenceEntries << render(Keys::promptTone, variables);
    if (!context.style.isEmpty())
        referenceEntries << render(Keys::promptStyle, variables);
    if (!context.background.isEmpty())
        referenceEntries << render(Keys::promptBackground, variables);
    if (!glossary.isEmpty())
        referenceEntries << render(Keys::promptGlossary, variables);

    QStringList fragments;
    if (!referenceEntries.isEmpty()) {
        const QString header = render(Keys::promptReference, variables).trimmed();
        fragments << (header.isEmpty() ? referenceEntries.join(QString())
                                       : header + QStringLiteral("\n\n") + referenceEntries.join(QString()));
    }
    fragments << render(Keys::promptDefault, variables);

    QStringList parts;
    for (const QString& fragment : fragments) {
        const QString trimmed = fragment.trimmed();
        if (!trimmed.isEmpty())
            parts << trimmed;
    }

    Result result;
    result.system = render(Keys::promptSystem, variables).trimmed();
    result.user = parts.join(QStringLiteral("\n\n"));
    return result;
}

QString PromptBuilder::systemPrompt(const TranslationContext& context)
{
    return render(Keys::promptSystem, variablesFor(context)).trimmed();
}

QString PromptBuilder::candidatePrompt(const QString& key, const TranslationContext& context)
{
    return render(key, variablesFor(context));
}
