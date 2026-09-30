#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include "core/models/Glossary.h"

struct TranslationContext
{
    QString sourceText;
    QString translatedText;
    // Selection offered as {selected_word} and, wrapped in the markers, as
    // {selected_fragment}; both stay empty on a translation request.
    QString selectedWord;
    QString selectedFragment;
    QString sourceLang = QStringLiteral("auto");
    QString targetLang = QStringLiteral("en");
    QString tone;
    QString style;
    QString background;
    bool glossaryEnabled = true;
    QVector<GlossaryEntry> glossary;
};

// Assembles chat prompts from the user-editable templates in the configuration.
// Each template carries its own labels and fences; an entry is emitted only
// while its variable holds a value, so unset options stay out of the prompt.
class PromptBuilder
{
public:
    struct Result
    {
        QString system;
        QString user;
    };

    static Result build(const TranslationContext& context);
    // Renders one template on its own, for a request that is not a translation.
    static QString candidatePrompt(const QString& key, const TranslationContext& context);
    // System prompt for a request that is not a translation.
    static QString systemPrompt(const TranslationContext& context);
    static QStringList knownPlaceholders();
    static QString substitute(QString text, const QHash<QString, QString>& variables);
    // Renders the glossary as a JSON array so the model reads the pairs as data.
    // A term with no target is mapped to itself.
    static QString glossaryData(const QVector<GlossaryEntry>& entries);

private:
    // Every placeholder is present, so any template can reach any of them.
    static QHash<QString, QString> variablesFor(const TranslationContext& context);
    static QString render(const QString& key, const QHash<QString, QString>& variables);
};
