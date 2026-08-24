#include "CandidatePopup.h"

#include "core/translation/TranslationEngine.h"

#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

CandidatePopup::CandidatePopup(TranslationEngine* engine, QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
    , m_engine(engine)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(4);
    m_header = new QLabel(this);
    QFont headerFont = m_header->font();
    headerFont.setBold(true);
    m_header->setFont(headerFont);
    m_status = new QLabel(this);
    m_list = new QListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->hide();
    layout->addWidget(m_header);
    layout->addWidget(m_status);
    layout->addWidget(m_list);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        emit candidateChosen(m_replaceTarget.isEmpty() ? m_word : m_replaceTarget,
                             item->text(), m_cursor);
        close();
    });
}

void CandidatePopup::openFor(const QString& word,
                             const QPoint& globalPos,
                             const QString& sourceText,
                             const QString& translatedText,
                             const QString& targetLang,
                             const QTextCursor& cursor)
{
    m_word = word;
    m_replaceTarget.clear();
    m_cursor = cursor;
    m_header->setText(word);
    move(globalPos + QPoint(8, 10));
    show();
    raise();
    setFocus();

    m_status->setText(tr("Fetching alternatives..."));
    m_list->clear();
    m_list->hide();
    adjustSize();

    m_engine->requestCandidates(
        sourceText, translatedText, word, targetLang,
        [this](const TranslationEngine::CandidateResult& result) {
            if (!isVisible())
                return;
            m_replaceTarget = result.replaceTarget;
            m_list->clear();
            if (result.options.isEmpty()) {
                m_status->setText(tr("No alternatives found"));
                m_list->hide();
            } else {
                m_status->hide();
                m_list->addItems(result.options);
                m_list->show();
            }
            adjustSize();
        },
        [this](const QString& message) {
            if (!isVisible())
                return;
            m_status->setText(message);
            m_list->hide();
            adjustSize();
        });
}

void CandidatePopup::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}
