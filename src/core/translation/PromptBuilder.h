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
// The reference block is appended dynamically: a block only appears when its
// variable holds a value, so an unset option never reaches the model.
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
    // Renders one dynamic entry of the reference block, indenting wrapped lines
    // so a multi-line body stays inside its fenced value.
    static QString referenceEntry(const QString& label, const QString& body, const QString& fenceLanguage = QString());
    // Joins the non-empty reference entries under the localized header.
    static QString referenceBlock(const QStringList& entries, const QString& uiLanguage);

private:
    static QString templateFor(const QString& name, const QString& uiLanguage);
};
