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
        emit candidateChosen(item->data(Qt::UserRole).toInt(),
                             item->data(Qt::UserRole + 1).toString(), item->text());
        close();
    });
}

void CandidatePopup::openFor(const QString& word,
                             int selectionStart,
                             int selectionEnd,
                             const QPoint& globalPos,
                             const TranslationContext& context)
{
    m_header->setText(word);
    move(globalPos + QPoint(8, 10));
    show();
    raise();
    setFocus();

    // The status label stays hidden after a rendered list, so a new lookup has
    // to bring it back.
    m_status->setText(tr("Fetching alternatives..."));
    m_status->show();
    m_list->clear();
    m_list->hide();
    adjustSize();

    m_engine->requestCandidates(
        context, selectionStart, selectionEnd,
        [this](const QVector<TranslationEngine::CandidateGroup>& groups) {
            if (!isVisible())
                return;

            // Groups arrive already resolved to a span of the translation, so a
            // hallucinated or ambiguous target never reaches the list.
            m_list->clear();
            for (const TranslationEngine::CandidateGroup& group : groups) {
                for (const QString& option : group.options) {
                    auto* item = new QListWidgetItem(option, m_list);
                    item->setData(Qt::UserRole, group.start);
                    item->setData(Qt::UserRole + 1, group.target);
                }
            }

            if (m_list->count() == 0) {
                m_status->setText(tr("No alternatives found"));
                m_list->hide();
            } else {
                m_status->hide();
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

void CandidatePopup::hideEvent(QHideEvent* event)
{
    m_engine->cancelCandidates();
    QWidget::hideEvent(event);
}

void CandidatePopup::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}
