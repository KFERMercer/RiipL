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

// Handed to the model for a neighbour a document edge does not have, instead of
// an empty block.
const QString kNoAdjacentWindow = QStringLiteral("None");

// Trims every fragment and joins the non-empty ones.
QString joined(const QStringList& fragments)
{
    QStringList parts;
    for (const QString& fragment : fragments) {
        const QString trimmed = fragment.trimmed();
        if (!trimmed.isEmpty())
            parts << trimmed;
    }
    return parts.join(QStringLiteral("\n\n"));
}

// A multi-line value takes the indentation of the line its placeholder sits on,
// read from the template rather than from what an earlier placeholder left there.
QString indentedValue(const QString& value, const QString& text, int lineStart, int placeholder)
{
    if (!value.contains(QLatin1Char('\n')))
        return value;
    QString indent;
    for (int at = lineStart; at < placeholder && text.at(at).isSpace(); ++at)
        indent += text.at(at);
    if (indent.isEmpty())
        return value;
    QString result = value;
    result.replace(QLatin1Char('\n'), QLatin1Char('\n') + indent);
    return result;
}

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

QString PromptBuilder::documentWindowData(const QStringList& lines)
{
    if (lines.isEmpty())
        return QString();
    // QJsonObject would order the keys as text, which puts line 10 before line 2,
    // so the object is written key by key in line order and only the values go
    // through the JSON writer, which escapes them.
    const auto escaped = [](const QString& line) {
        const QString wrapped = QString::fromUtf8(
            QJsonDocument(QJsonArray{line}).toJson(QJsonDocument::Compact));
        return wrapped.mid(1, wrapped.size() - 2);
    };

    QStringList entries;
    entries.reserve(lines.size());
    for (int index = 0; index < lines.size(); ++index) {
        entries.append(QStringLiteral("    \"%1\": %2").arg(index + 1).arg(escaped(lines.at(index))));
    }
    return QStringLiteral("{\n") + entries.join(QStringLiteral(",\n")) + QStringLiteral("\n}");
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
        QStringLiteral("mark_right"),
        QStringLiteral("window"),
        QStringLiteral("window_lines"),
        QStringLiteral("prev_segment"),
        QStringLiteral("next_segment")
    };
}

QString PromptBuilder::substitute(const QString& text, const QHash<QString, QString>& variables)
{
    // One pass over the template: text a placeholder brings in is never scanned
    // again, and only a brace run naming a variable counts as a placeholder.
    QString result;
    result.reserve(text.size());
    int lineStart = 0;
    int at = 0;
    while (at < text.size()) {
        const QChar character = text.at(at);
        if (character == QLatin1Char('\n')) {
            result += character;
            lineStart = ++at;
            continue;
        }
        if (character == QLatin1Char('{')) {
            int end = at + 1;
            while (end < text.size()
                   && (text.at(end).isLetterOrNumber() || text.at(end) == QLatin1Char('_')))
                ++end;
            const bool named = end > at + 1 && end < text.size()
                && text.at(end) == QLatin1Char('}');
            const auto value = named ? variables.constFind(text.mid(at + 1, end - at - 1))
                                     : variables.constEnd();
            if (value != variables.constEnd()) {
                result += indentedValue(*value, text, lineStart, at);
                at = end + 1;
                continue;
            }
        }
        result += character;
        ++at;
    }
    return result;
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
    // Filled in by the document request; empty here so their tokens never
    // reach another prompt.
    variables.insert(QStringLiteral("window"), QString());
    variables.insert(QStringLiteral("window_lines"), QString());
    variables.insert(QStringLiteral("prev_segment"), QString());
    variables.insert(QStringLiteral("next_segment"), QString());
    return variables;
}

QString PromptBuilder::render(const QString& key, const QHash<QString, QString>& variables)
{
    return substitute(ConfigManager::instance()->stringValue(key), variables);
}

QStringList PromptBuilder::referenceEntries(const TranslationContext& context,
                                            const QHash<QString, QString>& variables)
{
    QStringList entries;
    // The default tone is the absence of a tone, so it is skipped like an empty one.
    if (!context.tone.isEmpty() && context.tone != QLatin1String("default"))
        entries << render(Keys::promptTone, variables);
    if (!context.style.isEmpty())
        entries << render(Keys::promptStyle, variables);
    if (!context.background.isEmpty())
        entries << render(Keys::promptBackground, variables);
    if (!variables.value(QStringLiteral("glossary")).isEmpty())
        entries << render(Keys::promptGlossary, variables);
    return entries;
}

QString PromptBuilder::referenceBlock(const TranslationContext& context,
                                      const QHash<QString, QString>& variables)
{
    const QStringList entries = referenceEntries(context, variables);
    if (entries.isEmpty())
        return QString();
    const QString header = render(Keys::promptReference, variables).trimmed();
    const QString body = entries.join(QString());
    return header.isEmpty() ? body : header + QStringLiteral("\n\n") + body;
}

PromptBuilder::Result PromptBuilder::build(const TranslationContext& context)
{
    const QHash<QString, QString> variables = variablesFor(context);
    Result result;
    result.system = render(Keys::promptSystem, variables).trimmed();
    result.user = joined({referenceBlock(context, variables),
                          render(Keys::promptDefault, variables)});
    return result;
}

PromptBuilder::Result PromptBuilder::buildDocument(const TranslationContext& context,
                                                   const DocumentWindowPrompt& window)
{
    QHash<QString, QString> variables = variablesFor(context);
    variables.insert(QStringLiteral("window"), documentWindowData(window.lines));
    variables.insert(QStringLiteral("window_lines"), QString::number(window.lines.size()));
    variables.insert(QStringLiteral("prev_segment"),
                     window.previous.isEmpty() ? kNoAdjacentWindow : window.previous);
    variables.insert(QStringLiteral("next_segment"),
                     window.next.isEmpty() ? kNoAdjacentWindow : window.next);

    Result result;
    result.system = render(Keys::promptSystem, variables).trimmed();
    result.user = joined({referenceBlock(context, variables),
                          render(Keys::promptDocument, variables)});
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
