#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include "core/models/Glossary.h"

struct TranslationContext
{
    QString sourceText;
    QString translatedText;
    // Selection in the translation, offered as {selected_word} and, wrapped in
    // the markers, as {selected_fragment}. Empty on a translation request.
    QString selectedWord;
    QString selectedFragment;
    QString sourceLang = QStringLiteral("auto");
    QString targetLang = QStringLiteral("en");
    QString tone;
    QString style;
    QString background;
    bool glossaryEnabled = true;
    QVector<GlossaryEntry> glossary;
    QString uiLanguage = QStringLiteral("en");
};

// Assembles chat prompts from the user-editable templates in the configuration.
// Each template carries its own labels and fences together with the placeholders
// it interpolates, so presentation lives in the template rather than here. An
// entry is emitted only while its variable holds a value, which keeps unset
// options out of the prompt.
class PromptBuilder
{
public:
    struct Result
    {
        QString system;
        QString user;
    };

    static Result build(const TranslationContext& context);
    // Renders any named template, offering it every placeholder the context
    // carries.
    static QString candidatePrompt(const QString& name, const TranslationContext& context);
    // System prompt for a request that is not a translation.
    static QString systemPrompt(const TranslationContext& context);
    static QStringList knownPlaceholders();
    static QString substitute(QString text, const QHash<QString, QString>& variables);
    // Renders the glossary as a JSON array, so the model reads the pairs as
    // structured data instead of prose. A term with no target is mapped to
    // itself.
    static QString glossaryData(const QVector<GlossaryEntry>& entries);

private:
    static QString templateFor(const QString& name, const QString& uiLanguage);
    // Placeholder values a request interpolates. Every placeholder is present so
    // that any template can reach any of them.
    static QHash<QString, QString> variablesFor(const TranslationContext& context);
    static QString render(const QString& name, const QString& uiLanguage,
                          const QHash<QString, QString>& variables);
};
