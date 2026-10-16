#include "StatusPresenter.h"

#include <QLabel>
#include <QPalette>

#include <utility>

#include "ui/widgets/ThemeColors.h"

StatusPresenter::StatusPresenter(QLabel* label)
    : m_label(label)
{
    // A status can repeat a path the user typed, so it takes no markup.
    m_label->setTextFormat(Qt::PlainText);
}

void StatusPresenter::show(TextProvider text)
{
    render(std::move(text), false);
}

void StatusPresenter::showFailure(TextProvider text)
{
    render(std::move(text), true);
}

void StatusPresenter::refresh()
{
    if (m_text)
        render(m_text, m_failure);
}

void StatusPresenter::render(TextProvider text, bool failure)
{
    m_text = std::move(text);
    m_failure = failure;
    m_label->setText(m_text ? m_text() : QString());
    if (m_failure)
        ThemeColors::setTextColor(m_label, ThemeColors::errorText(m_label));
    else
        m_label->setPalette(QPalette());
}
