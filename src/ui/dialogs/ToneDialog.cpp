#include "ToneDialog.h"

#include "ui/widgets/AppIcons.h"
#include "ui/widgets/ConfigEditors.h"
#include "ui/widgets/MessageBoxes.h"
#include "ui/widgets/WindowState.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTableView>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace {
constexpr int kNameColumn = 0;
constexpr int kKeyColumn = 1;
}

ToneDialog::ToneDialog(const QVector<CustomTone>& customTones, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Tones"));

    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(tr("Preset tones"), this));
    auto* presets = new QTreeView(this);
    presets->setRootIsDecorated(false);
    presets->setAlternatingRowColors(true);
    presets->setAllColumnsShowFocus(true);
    presets->setSelectionMode(QAbstractItemView::NoSelection);
    presets->header()->setSectionResizeMode(QHeaderView::Stretch);
    presets->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    presets->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    layout->addWidget(presets);

    auto* presetModel = new QStandardItemModel(this);
    presetModel->setColumnCount(2);
    presetModel->setHorizontalHeaderLabels({tr("Display name"), tr("Key")});
    for (const ToneItem& item : Tones::presets()) {
        auto* name = new QStandardItem(toneLabel(item.key));
        name->setEditable(false);
        auto* key = new QStandardItem(item.key);
        key->setEditable(false);
        presetModel->appendRow({name, key});
    }
    presets->setModel(presetModel);

    layout->addWidget(new QLabel(tr("Custom tones"), this));
    m_custom = new QTableView(this);
    m_custom->setAlternatingRowColors(true);
    m_custom->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_custom->setSelectionMode(QAbstractItemView::SingleSelection);
    m_custom->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_custom->verticalHeader()->setVisible(false);
    layout->addWidget(m_custom, 1);

    m_customModel = new QStandardItemModel(this);
    m_customModel->setColumnCount(2);
    m_customModel->setHorizontalHeaderLabels({tr("Display name"), tr("Key")});
    m_custom->setModel(m_customModel);

    loadTones(customTones);

    auto* buttonRow = new QHBoxLayout();
    m_moveUpButton = new QToolButton(this);
    m_moveUpButton->setIcon(AppIcons::moveUp());
    m_moveUpButton->setToolTip(tr("Move up"));
    m_moveUpButton->setAccessibleName(tr("Move up"));
    m_moveDownButton = new QToolButton(this);
    m_moveDownButton->setIcon(AppIcons::moveDown());
    m_moveDownButton->setToolTip(tr("Move down"));
    m_moveDownButton->setAccessibleName(tr("Move down"));
    auto* addButton = new QPushButton(tr("Add"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    buttonRow->addWidget(m_moveUpButton);
    buttonRow->addWidget(m_moveDownButton);
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(m_removeButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(m_moveUpButton, &QToolButton::clicked, this, [this]() { moveTone(-1); });
    connect(m_moveDownButton, &QToolButton::clicked, this, [this]() { moveTone(1); });
    connect(addButton, &QPushButton::clicked, this, &ToneDialog::addTone);
    connect(m_removeButton, &QPushButton::clicked, this, &ToneDialog::removeTone);
    connect(m_custom->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &ToneDialog::refreshButtons);
    connect(m_customModel, &QAbstractItemModel::rowsInserted, this, &ToneDialog::refreshButtons);
    connect(m_customModel, &QAbstractItemModel::rowsRemoved, this, &ToneDialog::refreshButtons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_initialTones = this->customTones();
    refreshButtons();
    WindowState::track(this, WindowState::Id::tones);
}

void ToneDialog::reject()
{
    if (isDirty() && !MessageBoxes::confirmDiscard(this))
        return;

    QDialog::reject();
}

bool ToneDialog::isDirty() const
{
    return Tones::customToJson(customTones()) != Tones::customToJson(m_initialTones);
}

void ToneDialog::loadTones(const QVector<CustomTone>& stored)
{
    m_customModel->removeRows(0, m_customModel->rowCount());
    for (const CustomTone& tone : Tones::customNormalized(stored))
        m_customModel->appendRow(rowFor(tone));
}

QList<QStandardItem*> ToneDialog::rowFor(const CustomTone& tone) const
{
    return {new QStandardItem(tone.name), new QStandardItem(tone.key)};
}

QVector<CustomTone> ToneDialog::customTones() const
{
    QVector<CustomTone> result;
    result.reserve(m_customModel->rowCount());
    for (int row = 0; row < m_customModel->rowCount(); ++row) {
        CustomTone tone;
        if (const QStandardItem* key = m_customModel->item(row, kKeyColumn))
            tone.key = key->text();
        if (const QStandardItem* name = m_customModel->item(row, kNameColumn))
            tone.name = name->text();
        result.append(tone);
    }
    return Tones::customNormalized(result);
}

int ToneDialog::currentRow() const
{
    const QModelIndex index = m_custom->currentIndex();
    return index.isValid() ? index.row() : -1;
}

void ToneDialog::refreshButtons()
{
    const int row = currentRow();
    const bool hasSelection = row >= 0;
    m_removeButton->setEnabled(hasSelection);
    m_moveUpButton->setEnabled(hasSelection && row > 0);
    m_moveDownButton->setEnabled(hasSelection && row < m_customModel->rowCount() - 1);
}

void ToneDialog::addTone()
{
    const int row = m_customModel->rowCount();
    m_customModel->appendRow(rowFor(CustomTone{}));
    const QModelIndex index = m_customModel->index(row, kNameColumn);
    m_custom->setCurrentIndex(index);
    m_custom->edit(index);
}

void ToneDialog::removeTone()
{
    const int row = currentRow();
    if (row < 0)
        return;
    m_customModel->removeRow(row);
    refreshButtons();
}

void ToneDialog::moveTone(int offset)
{
    const int row = currentRow();
    if (row < 0)
        return;
    const int target = row + offset;
    if (target < 0 || target >= m_customModel->rowCount())
        return;
    // QStandardItemModel offers no moveRows, so the row is taken out and put back.
    m_customModel->insertRow(target, m_customModel->takeRow(row));
    m_custom->setCurrentIndex(m_customModel->index(target, kNameColumn));
    refreshButtons();
}
