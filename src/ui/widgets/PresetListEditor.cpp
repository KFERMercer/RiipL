#include "PresetListEditor.h"

#include "ui/widgets/AppIcons.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kNameColumn = 0;
// Captured values of a row, which its field columns render.
constexpr int kValuesRole = Qt::UserRole;

}

PresetListEditor::PresetListEditor(const QVector<FieldColumn>& fields, const Options& options,
                                   QWidget* parent)
    : QWidget(parent)
    , m_fields(fields)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    if (options.searchable) {
        auto* searchRow = new QHBoxLayout();
        searchRow->addWidget(new QLabel(tr("Search:"), this));
        m_search = new QLineEdit(this);
        m_search->setClearButtonEnabled(true);
        searchRow->addWidget(m_search, 1);
        layout->addLayout(searchRow);
    }

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
    // A preset matches when any of its columns contains the search text.
    m_proxy->setFilterKeyColumn(-1);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_tree->setModel(m_proxy);

    QStringList headers;
    headers.reserve(m_fields.size() + 1);
    headers.append(tr("Preset name"));
    for (const FieldColumn& field : m_fields)
        headers.append(field.header);
    m_model->setColumnCount(headers.size());
    m_model->setHorizontalHeaderLabels(headers);
    m_tree->setHeaderHidden(m_fields.isEmpty());
    m_tree->header()->setSectionResizeMode(kNameColumn, QHeaderView::ResizeToContents);
    for (int column = 1; column < headers.size(); ++column)
        m_tree->header()->setSectionResizeMode(column, QHeaderView::Stretch);

    auto* buttonRow = new QHBoxLayout();
    m_moveUpButton = new QToolButton(this);
    m_moveUpButton->setIcon(AppIcons::moveUp());
    m_moveUpButton->setToolTip(tr("Move up"));
    m_moveUpButton->setAccessibleName(tr("Move up"));
    m_moveDownButton = new QToolButton(this);
    m_moveDownButton->setIcon(AppIcons::moveDown());
    m_moveDownButton->setToolTip(tr("Move down"));
    m_moveDownButton->setAccessibleName(tr("Move down"));
    buttonRow->addWidget(m_moveUpButton);
    buttonRow->addWidget(m_moveDownButton);
    if (options.loadButton) {
        m_loadButton = new QPushButton(tr("Load preset"), this);
        m_loadButton->setToolTip(tr("Fill the fields with the highlighted preset"));
        buttonRow->addWidget(m_loadButton);
    }
    m_renameButton = new QPushButton(tr("Rename"), this);
    m_copyButton = new QPushButton(tr("Copy"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    buttonRow->addWidget(m_renameButton);
    buttonRow->addWidget(m_copyButton);
    buttonRow->addWidget(m_removeButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    // Filling the model emits per row, so the callers hear about the result once.
    const auto notify = [this]() {
        if (m_bulkChange)
            return;
        refreshButtons();
        emit changed();
    };

    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex&) {
        if (selectedIndex() >= 0)
            emit loadRequested();
    });
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [notify](const QModelIndex&, const QModelIndex&) { notify(); });
    connect(m_model, &QAbstractItemModel::dataChanged, this, notify);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, notify);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, notify);
    connect(m_model, &QAbstractItemModel::modelReset, this, notify);
    connect(m_moveUpButton, &QToolButton::clicked, this, [this]() { moveSelected(-1); });
    connect(m_moveDownButton, &QToolButton::clicked, this, [this]() { moveSelected(1); });
    if (m_loadButton)
        connect(m_loadButton, &QPushButton::clicked, this, &PresetListEditor::loadRequested);
    connect(m_renameButton, &QPushButton::clicked, this, &PresetListEditor::renameSelected);
    connect(m_copyButton, &QPushButton::clicked, this, &PresetListEditor::copySelected);
    connect(m_removeButton, &QPushButton::clicked, this, &PresetListEditor::removeSelected);
    if (m_search) {
        connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
            m_proxy->setFilterFixedString(text.trimmed());
            refreshButtons();
        });
    }

    refreshButtons();
}

void PresetListEditor::setPresets(const QVector<Preset>& presets)
{
    m_bulkChange = true;
    m_model->removeRows(0, m_model->rowCount());
    for (const Preset& preset : presets)
        m_model->appendRow(rowFor(preset));
    m_bulkChange = false;

    refreshButtons();
    emit changed();
}

QVector<Preset> PresetListEditor::presets() const
{
    QVector<Preset> result;
    result.reserve(m_model->rowCount());
    for (int row = 0; row < m_model->rowCount(); ++row) {
        Preset preset;
        preset.name = m_model->item(row, kNameColumn)->text();
        preset.values = m_model->item(row, kNameColumn)->data(kValuesRole).toJsonObject();
        result.append(preset);
    }
    return result;
}

int PresetListEditor::selectedIndex() const
{
    const QModelIndex index = m_tree->currentIndex();
    return index.isValid() ? m_proxy->mapToSource(index).row() : -1;
}

void PresetListEditor::setSelectedIndex(int index)
{
    m_tree->setCurrentIndex(index >= 0 && index < m_model->rowCount()
                                ? m_proxy->mapFromSource(m_model->index(index, kNameColumn))
                                : QModelIndex());
}

QString PresetListEditor::selectedName() const
{
    const int row = selectedIndex();
    return row >= 0 ? m_model->item(row, kNameColumn)->text() : QString();
}

QJsonObject PresetListEditor::selectedValues() const
{
    const int row = selectedIndex();
    if (row < 0)
        return QJsonObject();
    return storedValues(m_model->item(row, kNameColumn)->data(kValuesRole).toJsonObject());
}

void PresetListEditor::overwriteSelected(const QJsonObject& values)
{
    const int row = selectedIndex();
    if (row < 0)
        return;

    const QJsonObject stored = storedValues(values);
    m_model->item(row, kNameColumn)->setData(stored, kValuesRole);
    for (int column = 0; column < m_fields.size(); ++column) {
        const QString text = stored.value(m_fields.at(column).key).toString();
        QStandardItem* item = m_model->item(row, column + 1);
        item->setText(text);
        item->setToolTip(text);
    }
    refreshButtons();
}

void PresetListEditor::saveAs(const QString& name, const QJsonObject& values)
{
    const QJsonObject stored = storedValues(values);
    const QList<QStandardItem*> taken = m_model->findItems(name, Qt::MatchExactly, kNameColumn);
    if (taken.isEmpty()) {
        m_model->appendRow(rowFor(Preset{name, stored}));
        setSelectedIndex(m_model->rowCount() - 1);
    } else {
        const int existing = taken.first()->row();
        m_model->item(existing, kNameColumn)->setData(stored, kValuesRole);
        for (int column = 0; column < m_fields.size(); ++column) {
            const QString text = stored.value(m_fields.at(column).key).toString();
            QStandardItem* item = m_model->item(existing, column + 1);
            item->setText(text);
            item->setToolTip(text);
        }
        setSelectedIndex(existing);
    }
    refreshButtons();
}

void PresetListEditor::removeSelected()
{
    const int row = selectedIndex();
    if (row < 0)
        return;

    const QString name = m_model->item(row, kNameColumn)->text();
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Remove preset"), tr("Remove the preset \"%1\"?").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    m_model->removeRow(row);
    if (m_model->rowCount() > 0)
        setSelectedIndex((std::min)(row, m_model->rowCount() - 1));
    refreshButtons();
}

QJsonObject PresetListEditor::storedValues(const QJsonObject& values) const
{
    if (m_fields.isEmpty())
        return values;
    QStringList keys;
    keys.reserve(m_fields.size());
    for (const FieldColumn& field : m_fields)
        keys.append(field.key);
    return Presets::withDefaults(values, keys);
}

QList<QStandardItem*> PresetListEditor::rowFor(const Preset& preset) const
{
    const QJsonObject values = storedValues(preset.values);
    auto* name = new QStandardItem(preset.name);
    name->setEditable(false);
    name->setData(values, kValuesRole);
    name->setToolTip(preset.name);

    QList<QStandardItem*> row;
    row.reserve(m_fields.size() + 1);
    row.append(name);
    for (const FieldColumn& field : m_fields) {
        const QString text = values.value(field.key).toString();
        auto* item = new QStandardItem(text);
        item->setEditable(false);
        item->setToolTip(text);
        row.append(item);
    }
    return row;
}

void PresetListEditor::refreshButtons()
{
    const int row = selectedIndex();
    const bool hasSelection = row >= 0;
    // A filtered list hides the rows a move would swap them with, so reordering
    // is offered only while the whole list is in view.
    const bool filtered = m_search && !m_search->text().trimmed().isEmpty();
    m_moveUpButton->setEnabled(hasSelection && !filtered && row > 0);
    m_moveDownButton->setEnabled(hasSelection && !filtered && row < m_model->rowCount() - 1);
    if (m_loadButton)
        m_loadButton->setEnabled(hasSelection);
    m_renameButton->setEnabled(hasSelection);
    m_copyButton->setEnabled(hasSelection);
    m_removeButton->setEnabled(hasSelection);
}

void PresetListEditor::moveSelected(int offset)
{
    const int row = selectedIndex();
    if (row < 0)
        return;
    const int target = row + offset;
    if (target < 0 || target >= m_model->rowCount())
        return;
    // The model offers no moveRows, so the row is taken out and put back.
    m_model->insertRow(target, m_model->takeRow(row));
    setSelectedIndex(target);
    refreshButtons();
}

void PresetListEditor::renameSelected()
{
    const int row = selectedIndex();
    if (row < 0)
        return;

    QStandardItem* item = m_model->item(row, kNameColumn);
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Rename preset"), tr("Preset name"),
                                              QLineEdit::Normal, item->text(), &accepted).trimmed();
    if (!accepted || name.isEmpty())
        return;

    // Names must stay unique, since a preset is identified by its name.
    const QList<QStandardItem*> clashes = m_model->findItems(name, Qt::MatchExactly, kNameColumn);
    if (!clashes.isEmpty() && clashes.first() != item) {
        QMessageBox::warning(this, QCoreApplication::applicationName(),
                             tr("A preset named \"%1\" already exists.").arg(name));
        return;
    }

    item->setText(name);
    item->setToolTip(name);
    refreshButtons();
}

void PresetListEditor::copySelected()
{
    const int row = selectedIndex();
    if (row < 0)
        return;

    Preset copy;
    copy.name = uniqueName(m_model->item(row, kNameColumn)->text());
    copy.values = m_model->item(row, kNameColumn)->data(kValuesRole).toJsonObject();
    m_model->insertRow(row + 1, rowFor(copy));
    setSelectedIndex(row + 1);
    refreshButtons();
}

QStringList PresetListEditor::names() const
{
    QStringList result;
    result.reserve(m_model->rowCount());
    for (int row = 0; row < m_model->rowCount(); ++row)
        result.append(m_model->item(row, kNameColumn)->text());
    return result;
}

QString PresetListEditor::uniqueName(const QString& base) const
{
    // A name this editor generated for a copy is not a stem of its own, so copies
    // of copies keep counting on the name they were copied from. Both sides use
    // the same formats, which keeps the marker in step with the translation.
    const QStringList existing = names();
    QString stem = base;
    for (const QString& name : existing) {
        bool isCopyOfName = false;
        for (int number = 1; !isCopyOfName; ++number) {
            const QString candidate = number == 1 ? tr("%1 copy").arg(name)
                                                  : tr("%1 copy %2").arg(name).arg(number);
            if (candidate == base)
                isCopyOfName = true;
            else if (candidate.size() > base.size())
                break;
        }
        if (isCopyOfName) {
            stem = name;
            break;
        }
    }

    const auto taken = [&existing](const QString& name) { return existing.contains(name); };
    const QString firstCopy = tr("%1 copy").arg(stem);
    if (stem == base && !taken(firstCopy))
        return firstCopy;

    for (int number = 2;; ++number) {
        const QString candidate = tr("%1 copy %2").arg(stem).arg(number);
        if (!taken(candidate))
            return candidate;
    }
}
