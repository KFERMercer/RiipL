#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include "core/models/Glossary.h"

struct TranslationContext
{
    QString sourceText;
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
    static QString candidatePrompt(const QString& sourceText,
                                   const QString& translatedText,
                                   const QString& word,
                                   const QString& targetLang,
                                   const QString& uiLanguage);
    static QStringList knownPlaceholders();
    static QString substitute(QString text, const QHash<QString, QString>& variables);
    // Renders the glossary as a JSON array whose keys are localized, so the
    // model reads the pairs as structured data instead of prose. A term with
    // no target is mapped to itself.
    static QString glossaryData(const QVector<GlossaryEntry>& entries, const QString& uiLanguage);

private:
    static QString templateFor(const QString& name, const QString& uiLanguage);
};
