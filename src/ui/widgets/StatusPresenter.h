#pragma once

#include <QString>

#include <functional>

class QLabel;

// Status line of a window. The text is held as a provider rather than as a
// string, so a language change renders the current status again.
class StatusPresenter
{
public:
    using TextProvider = std::function<QString()>;

    explicit StatusPresenter(QLabel* label);

    void show(TextProvider text);
    // Renders the status in the error colour.
    void showFailure(TextProvider text);
    void refresh();

private:
    void render(TextProvider text, bool failure);

    QLabel* m_label;
    TextProvider m_text;
    bool m_failure = false;
};
