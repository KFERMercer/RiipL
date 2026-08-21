#include "GlossaryDialog.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "ui/widgets/ConfigEditors.h"

#include <QDialogButtonBox>
#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
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

    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({tr("Source term"), tr("Translation (leave empty to keep source)")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setColumnWidth(kSourceColumn, 220);
    layout->addWidget(m_table, 1);

    auto* buttonRow = new QHBoxLayout();
    auto addButton = new QPushButton(tr("Add"), this);
    auto removeButton = new QPushButton(tr("Remove"), this);
    auto upButton = new QToolButton(this);
    upButton->setText(QStringLiteral("\u2191"));
    auto downButton = new QToolButton(this);
    downButton->setText(QStringLiteral("\u2193"));
    auto importButton = new QPushButton(tr("Import JSON..."), this);
    auto exportButton = new QPushButton(tr("Export JSON..."), this);
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(removeButton);
    buttonRow->addWidget(upButton);
    buttonRow->addWidget(downButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(importButton);
    buttonRow->addWidget(exportButton);
    layout->addLayout(buttonRow);

    connect(addButton, &QPushButton::clicked, this, &GlossaryTable::addRow);
    connect(removeButton, &QPushButton::clicked, this, &GlossaryTable::removeSelected);
    connect(upButton, &QToolButton::clicked, this, [this]() { moveRow(-1); });
    connect(downButton, &QToolButton::clicked, this, [this]() { moveRow(1); });
    connect(importButton, &QPushButton::clicked, this, &GlossaryTable::importJson);
    connect(exportButton, &QPushButton::clicked, this, &GlossaryTable::exportJson);
    connect(m_filter, &QLineEdit::textChanged, this, [this]() { applyFilter(); });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) {
        if (!m_guard)
            emit entriesChanged();
        applyFilter();
    });
}

void GlossaryTable::setEntries(const QVector<GlossaryEntry>& entries)
{
    m_guard = true;
    m_table->setRowCount(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        auto* sourceItem = new QTableWidgetItem(entries.at(i).source);
        auto* targetItem = new QTableWidgetItem(entries.at(i).target);
        targetItem->setToolTip(tr("Leave empty to keep the term untranslated"));
        m_table->setItem(i, kSourceColumn, sourceItem);
        m_table->setItem(i, kTargetColumn, targetItem);
    }
    m_guard = false;
    applyFilter();
}

QVector<GlossaryEntry> GlossaryTable::entries() const
{
    QVector<GlossaryEntry> result;
    result.reserve(m_table->rowCount());
    for (int i = 0; i < m_table->rowCount(); ++i) {
        GlossaryEntry entry;
        entry.source = m_table->item(i, kSourceColumn)->text().trimmed();
        entry.target = m_table->item(i, kTargetColumn)->text().trimmed();
        if (!entry.source.isEmpty())
            result.append(entry);
    }
    return result;
}

void GlossaryTable::addRow()
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, kSourceColumn, new QTableWidgetItem());
    m_table->setItem(row, kTargetColumn, new QTableWidgetItem());
    m_table->editItem(m_table->item(row, kSourceColumn));
    applyFilter();
}

void GlossaryTable::removeSelected()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    m_table->removeRow(row);
    emit entriesChanged();
}

void GlossaryTable::moveRow(int offset)
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    const int target = row + offset;
    if (target < 0 || target >= m_table->rowCount())
        return;
    QVector<GlossaryEntry> current = entries();
    std::swap(current[row], current[target]);
    setEntries(current);
    m_table->selectRow(target);
    emit entriesChanged();
}

void GlossaryTable::applyFilter()
{
    const QString needle = m_filter ? m_filter->text().trimmed() : QString();
    for (int i = 0; i < m_table->rowCount(); ++i) {
        bool match = true;
        if (!needle.isEmpty()) {
            match = false;
            for (int column = 0; column < m_table->columnCount(); ++column) {
                const QTableWidgetItem* item = m_table->item(i, column);
                if (item && item->text().contains(needle, Qt::CaseInsensitive)) {
                    match = true;
                    break;
                }
            }
        }
        m_table->setRowHidden(i, !match);
    }
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
    emit entriesChanged();
}

void GlossaryTable::exportJson()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export glossary"),
                                                      QStringLiteral("glossary.json"),
                                                      tr("JSON files (*.json)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("RiipL"), tr("Cannot write file: %1").arg(path));
        return;
    }
    file.write(QJsonDocument(Glossary::toJson(entries())).toJson(QJsonDocument::Indented));
}

GlossaryDialog::GlossaryDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Glossary"));
    resize(560, 480);

    auto* layout = new QVBoxLayout(this);
    auto* enabledCheck = new ConfigCheckBox(Keys::glossaryEnabled, this);
    enabledCheck->box()->setText(tr("Enable glossary"));
    layout->addWidget(enabledCheck);

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
}
