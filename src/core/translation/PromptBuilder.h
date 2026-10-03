#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include "core/models/Glossary.h"

namespace CandidateMarks {

inline const QString selectionOpen = QStringLiteral("[[");
inline const QString selectionClose = QStringLiteral("]]");

}

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

// One window of a document: every document line of the window in reading order,
// and the neighbouring windows given as context. A line that repeats in the
// document is listed as often as it occurs, so an answer line maps onto the
// window position by position.
struct DocumentWindowPrompt
{
    QStringList lines;
    QString previous;
    QString next;
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
    // Builds the request for one window of a document, sharing the reference block
    // and the system prompt with a translation request.
    static Result buildDocument(const TranslationContext& context,
                                const DocumentWindowPrompt& window);
    // Renders one template on its own, for a request that is not a translation.
    static QString candidatePrompt(const QString& key, const TranslationContext& context);
    // System prompt for a request that is not a translation.
    static QString systemPrompt(const TranslationContext& context);
    static QStringList knownPlaceholders();
    static QString substitute(const QString& text, const QHash<QString, QString>& variables);
    // Renders the glossary as a JSON array so the model reads the pairs as data.
    // A term with no target is mapped to itself.
    static QString glossaryData(const QVector<GlossaryEntry>& entries);
    // Renders a window as a JSON object keyed by line number, so the model reads
    // the line count as data rather than as prose.
    static QString documentWindowData(const QStringList& lines);

private:
    // Every placeholder is present, so any template can reach any of them.
    static QHash<QString, QString> variablesFor(const TranslationContext& context);
    // Entries describing how to translate, in the order listed and only while
    // their option holds a value.
    static QStringList referenceEntries(const TranslationContext& context,
                                        const QHash<QString, QString>& variables);
    // The entries under their configured header, or nothing when none is set.
    static QString referenceBlock(const TranslationContext& context,
                                  const QHash<QString, QString>& variables);
    static QString render(const QString& key, const QHash<QString, QString>& variables);
};
