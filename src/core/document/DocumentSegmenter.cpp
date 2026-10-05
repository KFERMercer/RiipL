#include "DocumentSegmenter.h"

#include "utils/TextUtils.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <optional>

namespace {

// One numbered entry read from a raw answer.
struct AnswerEntry
{
    QString key;
    QString value;
};

// What a quoted run is read up to: a key at its colon, a value at the
// punctuation closing it.
enum class Quoted { Key, Value };

// Index of the first character from \p index that is not a space.
qsizetype skipSpace(const QString& text, qsizetype index)
{
    while (index < text.size() && text.at(index).isSpace())
        ++index;
    return index;
}

// Whether the quote at \p quote opens a "key": pair.
bool startsEntry(const QString& text, qsizetype quote)
{
    qsizetype cursor = quote + 1;
    while (cursor < text.size() && text.at(cursor) != QLatin1Char('"')
           && text.at(cursor) != QLatin1Char(':') && !text.at(cursor).isSpace()) {
        ++cursor;
    }
    if (cursor >= text.size() || text.at(cursor) != QLatin1Char('"'))
        return false;
    cursor = skipSpace(text, cursor + 1);
    return cursor < text.size() && text.at(cursor) == QLatin1Char(':');
}

// Whether the quote at \p quote ends the string it opened: a key before the
// punctuation of the answer behind it, a value only before the punctuation that
// closes it. A colon or comma the value holds stays in it, and a string the
// answer never closed ends with the text.
bool closesString(const QString& text, qsizetype quote, Quoted quoted)
{
    const qsizetype next = skipSpace(text, quote + 1);
    if (next >= text.size())
        return true;
    const QChar character = text.at(next);
    if (character == QLatin1Char(':'))
        return quoted == Quoted::Key;
    if (character == QLatin1Char('}') || character == QLatin1Char(']'))
        return true;
    if (character == QLatin1Char('"'))
        return startsEntry(text, next);
    if (character != QLatin1Char(','))
        return false;
    if (quoted == Quoted::Key)
        return true;
    // A comma ends a value when an entry or the object follows it; prose there
    // means the quote was part of the value.
    const qsizetype rest = skipSpace(text, next + 1);
    if (rest >= text.size() || text.at(rest) == QLatin1Char('}')
        || text.at(rest) == QLatin1Char(']')) {
        return true;
    }
    return text.at(rest) == QLatin1Char('"') && startsEntry(text, rest);
}

// Appends the character the escape at \p index stands for and returns the index
// after it; an escape the JSON grammar does not know is kept as written.
qsizetype appendEscape(const QString& text, qsizetype index, QString& value)
{
    if (index + 1 >= text.size()) {
        value.append(QLatin1Char('\\'));
        return text.size();
    }
    const QChar marker = text.at(index + 1);
    switch (marker.unicode()) {
    case u'"': value.append(QLatin1Char('"')); return index + 2;
    case u'\\': value.append(QLatin1Char('\\')); return index + 2;
    case u'/': value.append(QLatin1Char('/')); return index + 2;
    case u'b': value.append(QLatin1Char('\b')); return index + 2;
    case u'f': value.append(QLatin1Char('\f')); return index + 2;
    case u'n': value.append(QLatin1Char('\n')); return index + 2;
    case u'r': value.append(QLatin1Char('\r')); return index + 2;
    case u't': value.append(QLatin1Char('\t')); return index + 2;
    case u'u': {
        const QString digits = text.mid(index + 2, 4);
        bool ok = false;
        const char16_t unit = static_cast<char16_t>(digits.toUShort(&ok, 16));
        if (ok && digits.size() == 4) {
            value.append(QChar(unit));
            return index + 6;
        }
        value.append(QLatin1Char('\\'));
        return index + 1;
    }
    default:
        value.append(QLatin1Char('\\'));
        return index + 1;
    }
}

// Trims the spaces and the member punctuation off a value the answer left
// unclosed; both are structure, not text.
void trimUnclosedValue(QString& value)
{
    qsizetype end = value.size();
    while (end > 0 && value.at(end - 1).isSpace())
        --end;
    if (end > 0 && (value.at(end - 1) == QLatin1Char(',')
                    || value.at(end - 1) == QLatin1Char('}')
                    || value.at(end - 1) == QLatin1Char(']'))) {
        --end;
    }
    value.truncate(end);
}

// Whether the quote at \p quote opens the entry of a line number within
// \p lineCount, where a value the answer never closed ends.
bool startsNumberedEntry(const QString& text, qsizetype quote, int lineCount)
{
    if (!startsEntry(text, quote))
        return false;
    qsizetype end = quote + 1;
    while (end < text.size() && text.at(end) != QLatin1Char('"'))
        ++end;
    bool numbered = false;
    const int number = QStringView(text).mid(quote + 1, end - quote - 1).toInt(&numbered);
    return numbered && number >= 1 && number <= lineCount;
}

// Reads the quoted run opening at \p index and returns the index past its closing
// quote, or the index of the entry behind a value the answer never closed.
// Control characters left unescaped stay in the value.
qsizetype readQuoted(const QString& text, qsizetype index, QString& value, Quoted quoted,
                     int lineCount)
{
    qsizetype cursor = index + 1;
    while (cursor < text.size()) {
        const QChar character = text.at(cursor);
        if (character == QLatin1Char('\\')) {
            cursor = appendEscape(text, cursor, value);
            continue;
        }
        if (character == QLatin1Char('"')) {
            if (closesString(text, cursor, quoted))
                return cursor + 1;
            if (quoted == Quoted::Value && startsNumberedEntry(text, cursor, lineCount)) {
                trimUnclosedValue(value);
                return cursor;
            }
        }
        value.append(character);
        ++cursor;
    }
    if (quoted == Quoted::Value)
        trimUnclosedValue(value);
    return text.size();
}

// Occurrences of \p key as an object key in \p text. A repeated key is visible
// only on the raw answer, since the JSON parser keeps one value per key.
int keyOccurrences(const QString& text, const QString& key)
{
    const QString quoted = QStringLiteral("\"") + key + QStringLiteral("\"");
    int count = 0;
    qsizetype from = 0;
    while ((from = text.indexOf(quoted, from)) >= 0) {
        const qsizetype after = skipSpace(text, from + quoted.size());
        if (after < text.size() && text.at(after) == QLatin1Char(':'))
            ++count;
        from = after;
    }
    return count;
}

// Object of an answer that is valid JSON, unwrapped from a lone envelope such as
// {"lines": {...}}.
std::optional<QJsonObject> parseObject(const QString& text)
{
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isObject())
        return std::nullopt;
    QJsonObject object = document.object();
    while (object.size() == 1 && object.constBegin().value().isObject())
        object = object.constBegin().value().toObject();
    return object;
}

// Answers of \p object, which must hold the lines 1..\p count, each once and
// carrying text.
std::optional<QStringList> objectAnswers(const QJsonObject& object, const QString& text,
                                         int count)
{
    if (object.size() != count)
        return std::nullopt;

    QStringList answers;
    answers.reserve(count);
    for (int number = 1; number <= count; ++number) {
        const QString key = QString::number(number);
        if (keyOccurrences(text, key) > 1)
            return std::nullopt;
        const QString answer = object.value(key).toString().trimmed();
        if (answer.isEmpty())
            return std::nullopt;
        answers.append(answer);
    }
    return answers;
}

// Numbered entries of an answer that is not valid JSON, read pair by pair. Text
// around the answer is skipped, and so is a key that is no line number, since
// prose may hold quoted words followed by a colon.
QVector<AnswerEntry> scanEntries(const QString& text, int count)
{
    QVector<AnswerEntry> entries;
    // The scan gives up once the runs it read add up to this many times the
    // answer. A candidate naming no entry is retried one character later, so an
    // answer without punctuation could otherwise be read once per quote.
    constexpr qsizetype kScanPasses = 4;
    qsizetype budget = text.size() * kScanPasses;
    qsizetype index = 0;
    while (index < text.size()) {
        const qsizetype quote = text.indexOf(QLatin1Char('"'), index);
        if (quote < 0)
            break;
        QString key;
        qsizetype cursor = readQuoted(text, quote, key, Quoted::Key, count);

        cursor = skipSpace(text, cursor);
        if (cursor >= text.size() || text.at(cursor) != QLatin1Char(':')) {
            // A run reaching the end of the answer carries no entry: another one
            // would have ended the run at its colon.
            budget -= cursor - quote;
            if (cursor >= text.size() || budget < 0)
                break;
            index = quote + 1;
            continue;
        }
        cursor = skipSpace(text, cursor + 1);
        if (cursor >= text.size() || text.at(cursor) != QLatin1Char('"')) {
            index = cursor;
            continue;
        }

        QString value;
        index = readQuoted(text, cursor, value, Quoted::Value, count);
        entries.append({key, value});
    }
    return entries;
}

// Answers of entries read pair by pair: the line numbers must be 1..\p count,
// each once and carrying text.
std::optional<QStringList> scannedAnswers(const QVector<AnswerEntry>& entries, int count)
{
    QStringList answers;
    answers.reserve(count);
    for (int line = 0; line < count; ++line)
        answers.append(QString());

    QVector<bool> answered(count, false);
    for (const AnswerEntry& entry : entries) {
        bool numeric = false;
        const int number = entry.key.toInt(&numeric);
        if (!numeric)
            continue;
        if (entry.key != QString::number(number) || number < 1 || number > count
            || answered.at(number - 1)) {
            return std::nullopt;
        }
        const QString answer = entry.value.trimmed();
        if (answer.isEmpty())
            return std::nullopt;
        answered[number - 1] = true;
        answers[number - 1] = answer;
    }
    if (answered.contains(false))
        return std::nullopt;
    return answers;
}

// Answers of the document lines 1..\p count. An answer that parses as JSON is
// taken at its word, since a key it lacks or adds is a reason to ask again
// rather than to salvage; anything else is read leniently.
std::optional<QStringList> parseAnswers(const QString& response, int count)
{
    const QString raw = TextUtils::stripCodeFence(response);
    if (const std::optional<QJsonObject> object = parseObject(raw))
        return objectAnswers(*object, raw, count);
    return scannedAnswers(scanEntries(raw, count), count);
}

}

QString DocumentWindow::source() const
{
    QStringList texts;
    texts.reserve(lines.size());
    for (const DocumentLine& line : lines)
        texts.append(line.text);
    return texts.join(QLatin1Char('\n'));
}

QVector<DocumentWindow> DocumentSegmenter::partition(const QString& document, int wordLimit,
                                                     int lineLimit)
{
    QVector<DocumentWindow> windows;
    DocumentWindow window;
    int words = 0;
    QString whitespace;

    const auto closeWindow = [&windows, &window, &words]() {
        if (window.lines.isEmpty())
            return;
        windows.append(window);
        window = DocumentWindow();
        words = 0;
    };

    // A text ends where the whitespace closing its line starts.
    qsizetype cursor = 0;
    while (cursor < document.size()) {
        const qsizetype start = skipSpace(document, cursor);
        whitespace = document.mid(cursor, start - cursor);
        if (start >= document.size())
            break;

        const qsizetype lineBreak = document.indexOf(QLatin1Char('\n'), start);
        qsizetype end = lineBreak < 0 ? document.size() : lineBreak;
        while (document.at(end - 1).isSpace())
            --end;
        const QString text = document.mid(start, end - start);

        // Only the text is asked about, so spacing does not stop a fold.
        if (!window.lines.isEmpty() && window.lines.constLast().text == text) {
            window.lines.last().whitespaceBefore.append(whitespace);
        } else {
            // Only the lines reaching the model count, so a folded line costs no more
            // than its first occurrence; a line over the limit fills a window alone.
            const int lineWords = TextUtils::wordCount(text);
            if (!window.lines.isEmpty() && words + lineWords > wordLimit)
                closeWindow();

            words += lineWords;
            window.lines.append({text, {whitespace}});
            if (lineLimit != DocumentWindowLines::unlimitedSentinel
                && window.lines.size() >= lineLimit)
                closeWindow();
        }
        // The rest of the line opens the next run.
        whitespace.clear();
        cursor = end;
    }
    closeWindow();
    if (!windows.isEmpty())
        windows.last().whitespaceAfter = whitespace;
    return windows;
}

std::optional<QStringList> DocumentSegmenter::splitTranslation(const DocumentWindow& window,
                                                              const QString& response)
{
    // Duplicates stay collapsed in the window, so a line is answered once however
    // often it occurs in the document.
    const int count = window.lineCount();
    if (count == 0)
        return std::nullopt;
    return parseAnswers(response, count);
}

QString DocumentSegmenter::renderWindow(const DocumentWindow& window,
                                        const QStringList& translatedLines)
{
    const bool complete = translatedLines.size() == window.lines.size();

    QString rendered;
    for (int index = 0; index < window.lines.size(); ++index) {
        const DocumentLine& line = window.lines.at(index);
        const QString& text = complete ? translatedLines.at(index) : line.text;
        for (const QString& run : line.whitespaceBefore) {
            rendered += run;
            rendered += text;
        }
    }
    rendered += window.whitespaceAfter;
    return rendered;
}

QString DocumentSegmenter::assemble(const QVector<DocumentWindow>& windows,
                                    const QVector<QStringList>& translations)
{
    // The runs carry the breaks, so the windows follow one another.
    QString document;
    for (int index = 0; index < windows.size(); ++index) {
        document += renderWindow(windows.at(index),
                                 index < translations.size() ? translations.at(index)
                                                             : QStringList());
    }
    return document;
}
