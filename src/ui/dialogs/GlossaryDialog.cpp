#include "GlossaryDialog.h"

#include "core/models/Glossary.h"
#include "ui/widgets/AppIcons.h"
#include "ui/widgets/WindowState.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kSourceColumn = 0;
constexpr int kTargetColumn = 1;
}

GlossaryTable::GlossaryTable(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* topRow = new QHBoxLayout();
    topRow->addWidget(new QLabel(tr("Search:"), this));
    m_filter = new QLineEdit(this);
    m_filter->setClearButtonEnabled(true);
    topRow->addWidget(m_filter, 1);
    layout->addLayout(topRow);

    m_table = new QTableView(this);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    layout->addWidget(m_table, 1);

    m_model = new QStandardItemModel(this);
    m_model->setColumnCount(2);
    m_model->setHorizontalHeaderLabels(
        {tr("Source term"), tr("Translation (leave empty to keep source)")});
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    // A term matches when either of its columns contains the search text.
    m_proxy->setFilterKeyColumn(-1);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_table->setModel(m_proxy);

    auto* buttonRow = new QHBoxLayout();
    auto addButton = new QPushButton(tr("Add"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    m_moveUpButton = new QToolButton(this);
    m_moveUpButton->setIcon(AppIcons::moveUp());
    m_moveUpButton->setToolTip(tr("Move up"));
    m_moveDownButton = new QToolButton(this);
    m_moveDownButton->setIcon(AppIcons::moveDown());
    m_moveDownButton->setToolTip(tr("Move down"));
    auto importButton = new QPushButton(tr("Import JSON..."), this);
    auto exportButton = new QPushButton(tr("Export JSON..."), this);
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(m_removeButton);
    buttonRow->addWidget(m_moveUpButton);
    buttonRow->addWidget(m_moveDownButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(importButton);
    buttonRow->addWidget(exportButton);
    layout->addLayout(buttonRow);

    connect(addButton, &QPushButton::clicked, this, &GlossaryTable::addRow);
    connect(m_removeButton, &QPushButton::clicked, this, &GlossaryTable::removeSelected);
    connect(m_moveUpButton, &QToolButton::clicked, this, [this]() { moveRow(-1); });
    connect(m_moveDownButton, &QToolButton::clicked, this, [this]() { moveRow(1); });
    connect(importButton, &QPushButton::clicked, this, &GlossaryTable::importJson);
    connect(exportButton, &QPushButton::clicked, this, &GlossaryTable::exportJson);
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_proxy->setFilterFixedString(text.trimmed());
    });
    // The buttons act on the current row, so they follow the current index.
    connect(m_table->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &GlossaryTable::refreshButtons);
}

void GlossaryTable::setEntries(const QVector<GlossaryEntry>& entries)
{
    m_model->removeRows(0, m_model->rowCount());
    for (const GlossaryEntry& entry : entries) {
        auto* sourceItem = new QStandardItem(entry.source);
        auto* targetItem = new QStandardItem(entry.target);
        targetItem->setToolTip(tr("Leave empty to keep the term untranslated"));
        m_model->appendRow({sourceItem, targetItem});
    }
    refreshButtons();
}

QVector<GlossaryEntry> GlossaryTable::entries() const
{
    QVector<GlossaryEntry> result;
    result.reserve(m_model->rowCount());
    for (int i = 0; i < m_model->rowCount(); ++i) {
        GlossaryEntry entry;
        if (const QStandardItem* source = m_model->item(i, kSourceColumn))
            entry.source = source->text().trimmed();
        if (const QStandardItem* target = m_model->item(i, kTargetColumn))
            entry.target = target->text().trimmed();
        // Rows without a source term carry no glossary mapping and are dropped.
        if (!entry.source.isEmpty())
            result.append(entry);
    }
    return result;
}

void GlossaryTable::refreshButtons()
{
    const QModelIndex current = m_table->currentIndex();
    const bool hasSelection = current.isValid();
    m_removeButton->setEnabled(hasSelection);
    m_moveUpButton->setEnabled(hasSelection && current.row() > 0);
    m_moveDownButton->setEnabled(hasSelection && current.row() < m_proxy->rowCount() - 1);
}

void GlossaryTable::addRow()
{
    // A row the active filter hides could not be seen or edited.
    if (!m_filter->text().isEmpty())
        m_filter->clear();
    const int row = m_model->rowCount();
    m_model->appendRow({new QStandardItem, new QStandardItem});
    const QModelIndex index = m_proxy->mapFromSource(m_model->index(row, kSourceColumn));
    m_table->setCurrentIndex(index);
    m_table->edit(index);
    refreshButtons();
}

void GlossaryTable::removeSelected()
{
    const QModelIndex current = m_table->currentIndex();
    if (!current.isValid())
        return;
    m_model->removeRow(m_proxy->mapToSource(current).row());
    refreshButtons();
}

void GlossaryTable::moveRow(int offset)
{
    const QModelIndex current = m_table->currentIndex();
    if (!current.isValid())
        return;
    const int target = current.row() + offset;
    if (target < 0 || target >= m_proxy->rowCount())
        return;
    // Swapping the visible neighbours keeps filtered-out rows out of the move.
    const int row = m_proxy->mapToSource(current).row();
    const int targetRow = m_proxy->mapToSource(m_proxy->index(target, kSourceColumn)).row();
    // The row is taken out first, which leaves targetRow as the insert index for
    // either direction.
    m_model->insertRow(targetRow, m_model->takeRow(row));
    m_table->setCurrentIndex(m_proxy->index(target, kSourceColumn));
    // The current row is unchanged, so the end-of-list states are refreshed here.
    refreshButtons();
}

void GlossaryTable::importJson()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import glossary"),
                                                      QString(), tr("JSON files (*.json)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("RiipL"), tr("Cannot open file: %1").arg(path));
        return;
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        QMessageBox::warning(this, tr("RiipL"), tr("Invalid glossary JSON format"));
        return;
    }
    setEntries(Glossary::fromJson(doc.array()));
}

void GlossaryTable::exportJson()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export glossary"),
                                                      QStringLiteral("glossary.json"),
                                                      tr("JSON files (*.json)"));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("RiipL"), tr("Cannot write file: %1").arg(path));
        return;
    }
    const QJsonDocument doc(Glossary::toJson(entries()));
    const QByteArray json = doc.toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size() || !file.commit())
        QMessageBox::warning(this, tr("RiipL"), tr("Cannot write file: %1").arg(path));
}

GlossaryDialog::GlossaryDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Glossary"));

    auto* layout = new QVBoxLayout(this);
    m_table = new GlossaryTable(this);
    m_table->setEntries(Glossary::loadFromConfig().entries);
    layout->addWidget(m_table, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        Glossary glossary;
        glossary.entries = m_table->entries();
        glossary.saveToConfig();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    WindowState::track(this, WindowState::Id::glossary);
}
