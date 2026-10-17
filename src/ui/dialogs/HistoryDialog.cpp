#include "HistoryDialog.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/translation/Language.h"
#include "core/translation/Tone.h"
#include "ui/widgets/ConfigEditors.h"
#include "ui/widgets/WindowState.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>

namespace {

using ColumnText = QString (*)(const TranslationRecord&, const QVector<CustomLanguage>&);

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
     [](const TranslationRecord& record, const QVector<CustomLanguage>&) {
         return QDateTime::fromSecsSinceEpoch(record.timestamp)
             .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
     },
     QHeaderView::ResizeToContents, false},
    {QT_TRANSLATE_NOOP("HistoryDialog", "Source"),
     [](const TranslationRecord& record, const QVector<CustomLanguage>&) { return record.source; },
     QHeaderView::Stretch, true},
    {QT_TRANSLATE_NOOP("HistoryDialog", "Translation"),
     [](const TranslationRecord& record, const QVector<CustomLanguage>&) { return record.target; },
     QHeaderView::Stretch, true},
    {QT_TRANSLATE_NOOP("HistoryDialog", "Direction"),
     [](const TranslationRecord& record, const QVector<CustomLanguage>& custom) {
         return QStringLiteral("%1 → %2").arg(languageLabel(record.sourceLang, custom),
                                             languageLabel(record.targetLang, custom));
     },
     QHeaderView::ResizeToContents, false},
    {QT_TRANSLATE_NOOP("HistoryDialog", "Tone"),
     [](const TranslationRecord& record, const QVector<CustomLanguage>&) {
         return toneLabel(record.tone);
     },
     QHeaderView::ResizeToContents, false},
};

constexpr int kColumnCount = int(std::size(kColumns));
constexpr int kFirstColumn = 0;

QList<QStandardItem*> rowFor(const TranslationRecord& record,
                             const QVector<CustomLanguage>& custom)
{
    QList<QStandardItem*> row;
    row.reserve(kColumnCount);
    for (int column = 0; column < kColumnCount; ++column) {
        const QString text = kColumns[column].text(record, custom);
        auto* item = new QStandardItem(text);
        item->setEditable(false);
        if (kColumns[column].tooltip)
            item->setToolTip(text);
        row.append(item);
    }
    return row;
}

}

HistoryDialog::HistoryDialog(HistoryManager* history, QWidget* parent)
    : QDialog(parent)
    , m_history(history)
{
    setWindowTitle(tr("Translation history"));
    m_customLanguages = Languages::customFromJson(
        ConfigManager::instance()->value(Keys::translationCustomLanguages).toArray());

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
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_tree, 1);

    m_model = new QStandardItemModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    // A record matches when any of its columns contains the search text.
    m_proxy->setFilterKeyColumn(-1);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_tree->setModel(m_proxy);

    QStringList headers;
    headers.reserve(kColumnCount);
    for (const ColumnSpec& spec : kColumns)
        headers.append(QCoreApplication::translate("HistoryDialog", spec.header));
    m_model->setColumnCount(kColumnCount);
    m_model->setHorizontalHeaderLabels(headers);
    for (int column = 0; column < kColumnCount; ++column)
        m_tree->header()->setSectionResizeMode(column, kColumns[column].resizeMode);

    auto* buttonRow = new QHBoxLayout();
    m_reuseButton = new QPushButton(tr("Reuse"), this);
    m_deleteButton = new QPushButton(tr("Delete"), this);
    auto* clearButton = new QPushButton(tr("Clear all"), this);
    auto* closeButton = new QPushButton(tr("Close"), this);
    buttonRow->addWidget(m_reuseButton);
    buttonRow->addWidget(m_deleteButton);
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
    // The buttons act on the current row, so they follow the current index.
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &HistoryDialog::refreshButtons);
    connect(m_reuseButton, &QPushButton::clicked, this, [this]() {
        reuseItem(m_tree->currentIndex());
    });
    connect(m_deleteButton, &QPushButton::clicked, this, &HistoryDialog::removeCurrent);
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, QCoreApplication::applicationName(), tr("Delete all history records?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes)
            m_history->clear();
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    reload();
    WindowState::track(this, WindowState::Id::history);
}

// Only the rows that differ are added or removed, so a record arriving while the
// dialog is open costs one insertion.
void HistoryDialog::reload()
{
    if (!m_history)
        return;
    const QVector<TranslationRecord> records = m_history->records();

    int prefix = 0;
    while (prefix < m_records.size() && prefix < records.size()
           && m_records.at(prefix) == records.at(prefix))
        ++prefix;
    int suffix = 0;
    while (suffix < m_records.size() - prefix && suffix < records.size() - prefix
           && m_records.at(m_records.size() - 1 - suffix) == records.at(records.size() - 1 - suffix))
        ++suffix;

    const int removed = m_records.size() - prefix - suffix;
    if (removed > 0)
        m_model->removeRows(prefix, removed);
    for (int index = prefix; index < records.size() - suffix; ++index)
        m_model->insertRow(index, rowFor(records.at(index), m_customLanguages));

    m_records = records;
    refreshButtons();
}

void HistoryDialog::refreshButtons()
{
    const bool hasSelection = m_tree->currentIndex().isValid();
    m_reuseButton->setEnabled(hasSelection);
    m_deleteButton->setEnabled(hasSelection);
}

int HistoryDialog::currentRow() const
{
    const QModelIndex current = m_tree->currentIndex();
    return current.isValid() ? m_proxy->mapToSource(current).row() : -1;
}

void HistoryDialog::removeCurrent()
{
    const int row = currentRow();
    if (row < 0)
        return;

    m_history->removeRecord(row);
    // The record that took its place keeps the window usable without the mouse.
    if (m_model->rowCount() > 0) {
        m_tree->setCurrentIndex(m_proxy->mapFromSource(
            m_model->index((std::min)(row, m_model->rowCount() - 1), kFirstColumn)));
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
