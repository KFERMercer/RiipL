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
    ++cursor;
    while (cursor < text.size() && text.at(cursor).isSpace())
        ++cursor;
    return cursor < text.size() && text.at(cursor) == QLatin1Char(':');
}

// A quote ends a string when a separator follows it, so quotes inside a value do
// not cut it short. An unterminated string ends with the text.
bool closesString(const QString& text, qsizetype quote)
{
    qsizetype cursor = quote + 1;
    while (cursor < text.size() && text.at(cursor).isSpace())
        ++cursor;
    if (cursor >= text.size())
        return true;
    const QChar next = text.at(cursor);
    if (next == QLatin1Char(',') || next == QLatin1Char('}') || next == QLatin1Char(']')
        || next == QLatin1Char(':')) {
        return true;
    }
    return next == QLatin1Char('"') && startsEntry(text, cursor);
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

// Reads the quoted string opening at \p index and returns the index past its
// closing quote; control characters left unescaped stay in the value.
qsizetype readQuoted(const QString& text, qsizetype index, QString& value)
{
    qsizetype cursor = index + 1;
    while (cursor < text.size()) {
        const QChar character = text.at(cursor);
        if (character == QLatin1Char('\\')) {
            cursor = appendEscape(text, cursor, value);
            continue;
        }
        if (character == QLatin1Char('"') && closesString(text, cursor))
            return cursor + 1;
        value.append(character);
        ++cursor;
    }
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
        qsizetype after = from + quoted.size();
        while (after < text.size() && text.at(after).isSpace())
            ++after;
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
QVector<AnswerEntry> scanEntries(const QString& text)
{
    QVector<AnswerEntry> entries;
    qsizetype index = 0;
    while (index < text.size()) {
        const qsizetype quote = text.indexOf(QLatin1Char('"'), index);
        if (quote < 0)
            break;
        QString key;
        qsizetype cursor = readQuoted(text, quote, key);

        while (cursor < text.size() && text.at(cursor).isSpace())
            ++cursor;
        if (cursor >= text.size() || text.at(cursor) != QLatin1Char(':')) {
            index = quote + 1;
            continue;
        }
        ++cursor;
        while (cursor < text.size() && text.at(cursor).isSpace())
            ++cursor;
        if (cursor >= text.size() || text.at(cursor) != QLatin1Char('"')) {
            index = cursor;
            continue;
        }

        QString value;
        index = readQuoted(text, cursor, value);
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
    return scannedAnswers(scanEntries(raw), count);
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

QVector<DocumentWindow> DocumentSegmenter::partition(const QString& document, int charLimit,
                                                     int lineLimit)
{
    QVector<DocumentWindow> windows;
    DocumentWindow window;
    int characters = 0;
    int blanks = 0;

    const auto closeWindow = [&windows, &window, &characters]() {
        if (window.lines.isEmpty())
            return;
        windows.append(window);
        window = DocumentWindow();
        characters = 0;
    };

    const QStringList lines = document.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (line.trimmed().isEmpty()) {
            ++blanks;
            continue;
        }
        // Only adjacent duplicates collapse; a blank line between two equal
        // lines is part of the structure.
        if (blanks == 0 && !window.lines.isEmpty() && window.lines.constLast().text == line) {
            ++window.lines.last().repeats;
            continue;
        }

        // The joined window keeps the newlines that separate its lines, so the
        // limit applies to the text the model receives. A line longer than the
        // limit fills a window on its own.
        const int length = line.size();
        if (!window.lines.isEmpty() && characters + length + 1 > charLimit)
            closeWindow();

        characters += length + (window.lines.isEmpty() ? 0 : 1);
        window.lines.append({line, 1, blanks});
        blanks = 0;
        if (lineLimit != DocumentWindowLines::unlimitedSentinel
            && window.lines.size() >= lineLimit)
            closeWindow();
    }
    closeWindow();
    if (!windows.isEmpty())
        windows.last().trailingBlanks = blanks;
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

    QStringList lines;
    for (int line = 0; line < window.lines.size(); ++line) {
        const DocumentLine& entry = window.lines.at(line);
        for (int blank = 0; blank < entry.blanksBefore; ++blank)
            lines.append(QString());
        const QString text = complete ? translatedLines.at(line) : entry.text;
        for (int repeat = 0; repeat < entry.repeats; ++repeat)
            lines.append(text);
    }
    for (int blank = 0; blank < window.trailingBlanks; ++blank)
        lines.append(QString());
    return lines.join(QLatin1Char('\n'));
}

QString DocumentSegmenter::assemble(const QVector<DocumentWindow>& windows,
                                    const QVector<QStringList>& translations)
{
    QStringList texts;
    texts.reserve(windows.size());
    for (int index = 0; index < windows.size(); ++index) {
        texts.append(renderWindow(windows.at(index),
                                  index < translations.size() ? translations.at(index)
                                                              : QStringList()));
    }
    return texts.join(QLatin1Char('\n'));
}
