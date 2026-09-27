#include "ToneDialog.h"

#include "core/translation/Tone.h"
#include "ui/widgets/AppIcons.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int kNameColumn = 0;
constexpr int kKeyColumn = 1;
}

ToneDialog::ToneDialog(const QJsonArray& customTones, const QString& uiLanguage, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Tones"));

    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(tr("Preset tones"), this));
    m_presets = new QTreeWidget(this);
    m_presets->setHeaderLabels({tr("Display name"), tr("Key")});
    m_presets->setRootIsDecorated(false);
    m_presets->header()->setSectionResizeMode(QHeaderView::Stretch);
    for (const ToneItem& item : Tones::presets()) {
        auto* presetItem = new QTreeWidgetItem(m_presets);
        presetItem->setText(kNameColumn, Tones::presetDisplayName(item.key, uiLanguage));
        presetItem->setText(kKeyColumn, item.key);
        presetItem->setFlags(Qt::NoItemFlags);
    }
    m_presets->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_presets->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    layout->addWidget(m_presets);

    layout->addWidget(new QLabel(tr("Custom tones"), this));
    m_custom = new QTableWidget(0, 2, this);
    m_custom->setHorizontalHeaderLabels({tr("Display name"), tr("Key")});
    m_custom->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_custom->verticalHeader()->setVisible(false);
    m_custom->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_custom->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_custom, 1);

    loadTones(customTones);

    auto* buttonRow = new QHBoxLayout();
    m_addButton = new QPushButton(tr("Add"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    m_moveUpButton = new QToolButton(this);
    m_moveUpButton->setIcon(AppIcons::moveUp());
    m_moveUpButton->setToolTip(tr("Move up"));
    m_moveDownButton = new QToolButton(this);
    m_moveDownButton->setIcon(AppIcons::moveDown());
    m_moveDownButton->setToolTip(tr("Move down"));
    buttonRow->addWidget(m_addButton);
    buttonRow->addWidget(m_removeButton);
    buttonRow->addWidget(m_moveUpButton);
    buttonRow->addWidget(m_moveDownButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(m_addButton, &QPushButton::clicked, this, &ToneDialog::addTone);
    connect(m_removeButton, &QPushButton::clicked, this, &ToneDialog::removeTone);
    connect(m_moveUpButton, &QToolButton::clicked, this, [this]() { moveTone(-1); });
    connect(m_moveDownButton, &QToolButton::clicked, this, [this]() { moveTone(1); });
    connect(m_custom, &QTableWidget::itemSelectionChanged, this, &ToneDialog::refreshButtons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshButtons();
}

void ToneDialog::loadTones(const QJsonArray& stored)
{
    m_custom->setRowCount(stored.size());
    for (int i = 0; i < stored.size(); ++i) {
        const QJsonObject object = stored.at(i).toObject();
        m_custom->setItem(i, kKeyColumn, new QTableWidgetItem(object.value(QStringLiteral("key")).toString()));
        m_custom->setItem(i, kNameColumn, new QTableWidgetItem(object.value(QStringLiteral("name")).toString()));
    }
}

QJsonArray ToneDialog::toJson(const QVector<ToneItem>& tones)
{
    QJsonArray array;
    for (const ToneItem& tone : tones) {
        QJsonObject object;
        object.insert(QStringLiteral("key"), tone.key);
        object.insert(QStringLiteral("name"), tone.en);
        array.append(object);
    }
    return array;
}

QVector<ToneItem> ToneDialog::customTones() const
{
    QVector<ToneItem> result;
    for (int i = 0; i < m_custom->rowCount(); ++i) {
        ToneItem tone;
        tone.key = m_custom->item(i, kKeyColumn) ? m_custom->item(i, kKeyColumn)->text().trimmed() : QString();
        tone.en = m_custom->item(i, kNameColumn) ? m_custom->item(i, kNameColumn)->text().trimmed() : QString();
        if (tone.key.isEmpty())
            continue;
        if (tone.en.isEmpty())
            tone.en = tone.key;
        tone.zh = tone.en;
        result.append(tone);
    }
    return result;
}

void ToneDialog::refreshButtons()
{
    const int row = m_custom->currentRow();
    const bool hasSelection = row >= 0;
    m_removeButton->setEnabled(hasSelection);
    m_moveUpButton->setEnabled(hasSelection && row > 0);
    m_moveDownButton->setEnabled(hasSelection && row < m_custom->rowCount() - 1);
}

void ToneDialog::addTone()
{
    const int row = m_custom->rowCount();
    m_custom->insertRow(row);
    m_custom->setItem(row, kNameColumn, new QTableWidgetItem());
    m_custom->setItem(row, kKeyColumn, new QTableWidgetItem());
    m_custom->editItem(m_custom->item(row, kNameColumn));
    m_custom->setCurrentCell(row, kNameColumn);
    refreshButtons();
}

void ToneDialog::removeTone()
{
    const int row = m_custom->currentRow();
    if (row < 0)
        return;
    m_custom->removeRow(row);
    refreshButtons();
}

void ToneDialog::moveTone(int offset)
{
    const int row = m_custom->currentRow();
    if (row < 0)
        return;
    const int target = row + offset;
    if (target < 0 || target >= m_custom->rowCount())
        return;
    // moveRows keeps the row data and view state; destinationChild is the index
    // the row is inserted before, so a downward move lands past the row it swaps
    // with.
    m_custom->model()->moveRows(QModelIndex(), row, 1, QModelIndex(), offset < 0 ? target : target + 1);
    m_custom->selectRow(target);
    // The current row is unchanged, so the end-of-list states are refreshed here.
    refreshButtons();
}
