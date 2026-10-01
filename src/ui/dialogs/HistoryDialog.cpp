#include "HistoryDialog.h"

#include "core/translation/Language.h"
#include "core/translation/Tone.h"
#include "ui/widgets/ConfigEditors.h"
#include "ui/widgets/WindowState.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <iterator>

namespace {

using ColumnText = QString (*)(const TranslationRecord&);

struct ColumnSpec
{
    const char* header;
    ColumnText text;
    QHeaderView::ResizeMode resizeMode;
    // Elided cells offer their full value on hover.
    bool tooltip;
};

// Display order, content and sizing as one row per column, so moving a column
// is a single edit here.
const ColumnSpec kColumns[] = {
    {QT_TRANSLATE_NOOP("HistoryDialog", "Time"),
     [](const TranslationRecord& record) {
         return QDateTime::fromSecsSinceEpoch(record.timestamp)
             .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
     },
     QHeaderView::ResizeToContents, false},
    {QT_TRANSLATE_NOOP("HistoryDialog", "Source"),
     [](const TranslationRecord& record) { return record.source; },
     QHeaderView::Stretch, true},
    {QT_TRANSLATE_NOOP("HistoryDialog", "Translation"),
     [](const TranslationRecord& record) { return record.target; },
     QHeaderView::Stretch, true},
    {QT_TRANSLATE_NOOP("HistoryDialog", "Direction"),
     [](const TranslationRecord& record) {
         return QStringLiteral("%1 → %2").arg(languageLabel(record.sourceLang),
                                             languageLabel(record.targetLang));
     },
     QHeaderView::ResizeToContents, false},
    {QT_TRANSLATE_NOOP("HistoryDialog", "Tone"),
     [](const TranslationRecord& record) { return toneLabel(record.tone); },
     QHeaderView::ResizeToContents, false},
};

constexpr int kColumnCount = int(std::size(kColumns));

// The record index travels as item metadata rather than as another column.
constexpr int kRowIndexColumn = 0;

// Row index of the record an item shows, or -1 when there is no such item.
int rowIndex(QTreeWidgetItem* item)
{
    if (!item)
        return -1;
    const QVariant stored = item->data(kRowIndexColumn, Qt::UserRole);
    return stored.isValid() ? stored.toInt() : -1;
}

}

HistoryDialog::HistoryDialog(HistoryManager* history, QWidget* parent)
    : QDialog(parent)
    , m_history(history)
{
    setWindowTitle(tr("Translation history"));

    auto* layout = new QVBoxLayout(this);
    auto* topRow = new QHBoxLayout();
    topRow->addWidget(new QLabel(tr("Search:"), this));
    m_search = new QLineEdit(this);
    m_search->setClearButtonEnabled(true);
    topRow->addWidget(m_search, 1);
    layout->addLayout(topRow);

    m_tree = new QTreeWidget(this);
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_tree, 1);

    auto* buttonRow = new QHBoxLayout();
    auto reuseButton = new QPushButton(tr("Reuse"), this);
    auto deleteButton = new QPushButton(tr("Delete"), this);
    auto clearButton = new QPushButton(tr("Clear all"), this);
    auto closeButton = new QPushButton(tr("Close"), this);
    buttonRow->addWidget(reuseButton);
    buttonRow->addWidget(deleteButton);
    buttonRow->addWidget(clearButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    connect(m_history, &HistoryManager::changed, this, [this]() { reload(); });
    connect(m_search, &QLineEdit::textChanged, this, [this]() { applyFilter(); });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        reuseItem(item);
    });
    connect(reuseButton, &QPushButton::clicked, this, [this]() {
        reuseItem(m_tree->currentItem());
    });
    connect(deleteButton, &QPushButton::clicked, this, [this]() {
        const int index = rowIndex(m_tree->currentItem());
        if (index >= 0)
            m_history->removeRecord(index);
    });
    connect(clearButton, &QPushButton::clicked, this, [this]() { m_history->clear(); });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    reload();
    WindowState::track(this, WindowState::Id::history);
}

void HistoryDialog::reload()
{
    if (!m_history)
        return;
    m_records = m_history->records();

    QStringList headers;
    headers.reserve(kColumnCount);
    for (const ColumnSpec& spec : kColumns)
        headers.append(QCoreApplication::translate("HistoryDialog", spec.header));
    m_tree->setColumnCount(kColumnCount);
    m_tree->setHeaderLabels(headers);

    QHeaderView* header = m_tree->header();
    for (int i = 0; i < kColumnCount; ++i)
        header->setSectionResizeMode(i, kColumns[i].resizeMode);

    m_tree->clear();
    for (int i = 0; i < m_records.size(); ++i) {
        const TranslationRecord& record = m_records.at(i);
        auto* item = new QTreeWidgetItem(m_tree);
        for (int column = 0; column < kColumnCount; ++column) {
            const QString text = kColumns[column].text(record);
            item->setText(column, text);
            if (kColumns[column].tooltip)
                item->setToolTip(column, text);
        }
        item->setData(kRowIndexColumn, Qt::UserRole, i);
    }
    applyFilter();
}

void HistoryDialog::reuseItem(QTreeWidgetItem* item)
{
    const int index = rowIndex(item);
    if (index < 0 || index >= m_records.size())
        return;
    emit reuseRequested(m_records.at(index));
    accept();
}

void HistoryDialog::applyFilter()
{
    const QString needle = m_search ? m_search->text().trimmed() : QString();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_tree->topLevelItem(i);
        bool match = true;
        if (!needle.isEmpty()) {
            match = false;
            for (int column = 0; column < m_tree->columnCount(); ++column) {
                if (item->text(column).contains(needle, Qt::CaseInsensitive)) {
                    match = true;
                    break;
                }
            }
        }
        item->setHidden(!match);
    }
}
