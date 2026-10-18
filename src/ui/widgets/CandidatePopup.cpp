#include "CandidatePopup.h"

#include <QAction>

#include <algorithm>

namespace {

// A menu reads '&' as a mnemonic marker, so a literal ampersand is doubled.
QString menuText(const QString& text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('&'), QLatin1String("&&"));
    return escaped;
}

}

CandidatePopup::CandidatePopup(TranslationEngine* engine, QWidget* parent)
    : QMenu(parent)
    , m_engine(engine)
{
    connect(this, &QMenu::aboutToHide, this, [this]() {
        m_engine->cancelCandidates();
    });
}

void CandidatePopup::openFor(const QString& word,
                             int selectionStart,
                             int selectionEnd,
                             const QPoint& globalPos,
                             const TranslationContext& context)
{
    m_word = word;
    m_anchor = globalPos;
    showStatus(tr("Fetching alternatives..."));
    popup(m_anchor);

    m_engine->requestCandidates(
        context, selectionStart, selectionEnd,
        [this](const QVector<TranslationEngine::CandidateGroup>& groups) {
            if (isVisible())
                showCandidates(groups);
        },
        [this](const ApiClient::Error& failure) {
            if (isVisible())
                showStatus(failure.text());
        });
}

void CandidatePopup::resetEntries()
{
    clear();
    // A section renders its text literally, so only the actions are escaped.
    addSection(m_word);
}

void CandidatePopup::showStatus(const QString& text)
{
    resetEntries();
    addAction(menuText(text))->setEnabled(false);
    reposition();
}

void CandidatePopup::showCandidates(const QVector<TranslationEngine::CandidateGroup>& groups)
{
    const bool hasCandidates = std::any_of(
        groups.cbegin(), groups.cend(),
        [](const TranslationEngine::CandidateGroup& group) { return !group.options.isEmpty(); });
    if (!hasCandidates) {
        showStatus(tr("No alternatives found"));
        return;
    }

    resetEntries();
    for (const TranslationEngine::CandidateGroup& group : groups) {
        for (const TranslationEngine::CandidateOption& option : group.options) {
            QAction* action = addAction(menuText(option.text));
            connect(action, &QAction::triggered, this,
                    [this, start = option.start, length = option.length,
                     replacement = option.text]() {
                        emit candidateChosen(start, length, replacement);
                    });
        }
    }
    reposition();
}

void CandidatePopup::reposition()
{
    // Re-running the popup lets the native placement pull a grown menu back
    // inside the screen.
    if (isVisible())
        popup(m_anchor);
}
