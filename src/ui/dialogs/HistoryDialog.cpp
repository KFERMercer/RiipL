#include "HistoryDialog.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/translation/Language.h"
#include "core/translation/Tone.h"
#include "utils/GeometryUtils.h"

#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

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
        if (!item)
            return;
        const int index = item->data(0, Qt::UserRole).toInt();
        if (index >= 0 && index < m_records.size())
            emit reuseRequested(m_records.at(index));
        accept();
    });
    connect(reuseButton, &QPushButton::clicked, this, [this]() {
        QTreeWidgetItem* item = m_tree->currentItem();
        if (!item)
            return;
        const int index = item->data(0, Qt::UserRole).toInt();
        if (index >= 0 && index < m_records.size())
            emit reuseRequested(m_records.at(index));
        accept();
    });
    connect(deleteButton, &QPushButton::clicked, this, [this]() {
        QTreeWidgetItem* item = m_tree->currentItem();
        if (!item)
            return;
        const int index = item->data(0, Qt::UserRole).toInt();
        m_history->removeRecord(index);
    });
    connect(clearButton, &QPushButton::clicked, this, [this]() { m_history->clear(); });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    reload();
    resize(GeometryUtils::dialogInitialSize(this));
}

void HistoryDialog::reload()
{
    if (!m_history)
        return;
    m_records = m_history->records();

    QStringList headers = {tr("Time"), tr("Direction"), tr("Source"), tr("Translation"), tr("Tone")};
    m_tree->setColumnCount(headers.size());
    m_tree->setHeaderLabels(headers);
    m_tree->header()->resizeSection(0, 150);
    m_tree->header()->resizeSection(1, 120);
    m_tree->header()->resizeSection(2, 200);
    m_tree->header()->resizeSection(3, 200);
    m_tree->header()->setStretchLastSection(true);

    const QString uiLanguage = ConfigManager::instance()->resolvedUiLanguage();
    m_tree->clear();
    for (int i = 0; i < m_records.size(); ++i) {
        const TranslationRecord& record = m_records.at(i);
        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(0, QDateTime::fromSecsSinceEpoch(record.timestamp)
                             .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        item->setText(1, QStringLiteral("%1 → %2").arg(
                             Languages::displayName(record.sourceLang, uiLanguage),
                             Languages::displayName(record.targetLang, uiLanguage)));
        item->setText(2, record.source);
        item->setText(3, record.target);
        item->setText(4, Tones::presetDisplayName(record.tone, uiLanguage));
        item->setData(0, Qt::UserRole, i);
        item->setToolTip(2, record.source);
        item->setToolTip(3, record.target);
    }
    applyFilter();
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
