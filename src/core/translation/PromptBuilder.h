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
    QStringList preferences;
    bool glossaryEnabled = true;
    QVector<GlossaryEntry> glossary;
    QString uiLanguage = QStringLiteral("en");
};

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
    static QStringList glossaryLines(const QVector<GlossaryEntry>& entries);

private:
    static QString templateFor(const QString& name, const QString& uiLanguage);
};
