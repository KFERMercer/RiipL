#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include "core/models/Glossary.h"

struct TranslationContext
{
    QString sourceText;
    // Translation currently in the result pane. The candidate wording request
    // offers it as {translated_text} and slices {selected_fragment} out of it.
    QString translatedText;
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
    // Renders the candidate wording template used on a context window that does
    // not cover the whole translation. \p translatedText is the whole translation
    // and \p fragment the marked window around the selection. Templates pick
    // whichever of the two they need.
    static QString candidatePrompt(const QString& translatedText,
                                   const QString& fragment,
                                   const QString& word,
                                   const QString& targetLang,
                                   const QString& uiLanguage);
    // Renders the candidate wording template used when the window covers the
    // whole translation. \p translatedText is the unmarked translation, \p marked
    // the same text with the selection wrapped in the markers, and the request
    // carries \p sourceText as well.
    static QString candidateShortPrompt(const QString& translatedText,
                                        const QString& marked,
                                        const QString& word,
                                        const QString& sourceText,
                                        const QString& targetLang,
                                        const QString& uiLanguage);
    // System prompt for a request that is not a translation.
    static QString systemPrompt(const TranslationContext& context);
    static QStringList knownPlaceholders();
    static QString substitute(QString text, const QHash<QString, QString>& variables);
    // Renders the glossary as a JSON array whose keys are localized, so the
    // model reads the pairs as structured data instead of prose. A term with
    // no target is mapped to itself.
    static QString glossaryData(const QVector<GlossaryEntry>& entries, const QString& uiLanguage);

private:
    static QString templateFor(const QString& name, const QString& uiLanguage);
    // Placeholder values a request interpolates.
    static QHash<QString, QString> variablesFor(const TranslationContext& context);
    // Placeholder values shared by both candidate wording templates.
    static QHash<QString, QString> candidateVariables(const QString& translatedText,
                                                      const QString& marked,
                                                      const QString& word,
                                                      const QString& sourceText,
                                                      const QString& targetLang);
    static QString render(const QString& name, const QString& uiLanguage,
                          const QHash<QString, QString>& variables);
};
