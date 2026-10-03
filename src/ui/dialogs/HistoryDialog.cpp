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
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTreeView>
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

    m_tree = new QTreeView(this);
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_tree, 1);

    m_model = new QStandardItemModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    // A record matches when any of its columns contains the search text.
    m_proxy->setFilterKeyColumn(-1);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_tree->setModel(m_proxy);

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
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_proxy->setFilterFixedString(text.trimmed());
    });
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        reuseItem(index);
    });
    connect(reuseButton, &QPushButton::clicked, this, [this]() {
        reuseItem(m_tree->currentIndex());
    });
    connect(deleteButton, &QPushButton::clicked, this, [this]() {
        const QModelIndex index = m_tree->currentIndex();
        if (index.isValid())
            m_history->removeRecord(m_proxy->mapToSource(index).row());
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

    m_model->removeRows(0, m_model->rowCount());
    m_model->setColumnCount(kColumnCount);
    m_model->setHorizontalHeaderLabels(headers);

    QHeaderView* header = m_tree->header();
    for (int i = 0; i < kColumnCount; ++i)
        header->setSectionResizeMode(i, kColumns[i].resizeMode);

    for (int i = 0; i < m_records.size(); ++i) {
        const TranslationRecord& record = m_records.at(i);
        QList<QStandardItem*> row;
        row.reserve(kColumnCount);
        for (int column = 0; column < kColumnCount; ++column) {
            const QString text = kColumns[column].text(record);
            auto* item = new QStandardItem(text);
            item->setEditable(false);
            if (kColumns[column].tooltip)
                item->setToolTip(text);
            row.append(item);
        }
        m_model->appendRow(row);
    }
}

void HistoryDialog::reuseItem(const QModelIndex& index)
{
    if (!index.isValid())
        return;
    const int record = m_proxy->mapToSource(index).row();
    if (record < 0 || record >= m_records.size())
        return;
    emit reuseRequested(m_records.at(record));
    accept();
}
