#include "LanguageDialog.h"

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

LanguageDialog::LanguageDialog(const QVector<CustomLanguage>& customLanguages, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Languages"));

    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(tr("Built-in languages"), this));
    auto* presets = new QTreeView(this);
    presets->setRootIsDecorated(false);
    presets->setAlternatingRowColors(true);
    presets->setAllColumnsShowFocus(true);
    presets->setSelectionMode(QAbstractItemView::NoSelection);
    presets->header()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(presets, 1);

    auto* presetModel = new QStandardItemModel(this);
    presetModel->setColumnCount(2);
    presetModel->setHorizontalHeaderLabels({tr("Display name"), tr("Key")});
    for (const LangItem& item : Languages::all()) {
        if (item.code == QLatin1String("auto"))
            continue;
        auto* name = new QStandardItem(languageLabel(item.code));
        name->setEditable(false);
        // The key is what the prompt carries, so it stays untranslated.
        auto* key = new QStandardItem(QString::fromUtf8(item.en));
        key->setEditable(false);
        presetModel->appendRow({name, key});
    }
    presets->setModel(presetModel);

    layout->addWidget(new QLabel(tr("Custom languages"), this));
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

    loadLanguages(customLanguages);

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

    connect(m_moveUpButton, &QToolButton::clicked, this, [this]() { moveLanguage(-1); });
    connect(m_moveDownButton, &QToolButton::clicked, this, [this]() { moveLanguage(1); });
    connect(addButton, &QPushButton::clicked, this, &LanguageDialog::addLanguage);
    connect(m_removeButton, &QPushButton::clicked, this, &LanguageDialog::removeLanguage);
    connect(m_custom->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &LanguageDialog::refreshButtons);
    connect(m_customModel, &QAbstractItemModel::rowsInserted, this, &LanguageDialog::refreshButtons);
    connect(m_customModel, &QAbstractItemModel::rowsRemoved, this, &LanguageDialog::refreshButtons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_initialLanguages = this->customLanguages();
    refreshButtons();
    WindowState::track(this, WindowState::Id::languages);
}

void LanguageDialog::reject()
{
    if (isDirty() && !MessageBoxes::confirmDiscard(this))
        return;

    QDialog::reject();
}

bool LanguageDialog::isDirty() const
{
    return Languages::customToJson(customLanguages())
        != Languages::customToJson(m_initialLanguages);
}

void LanguageDialog::loadLanguages(const QVector<CustomLanguage>& stored)
{
    m_customModel->removeRows(0, m_customModel->rowCount());
    for (const CustomLanguage& language : Languages::customNormalized(stored))
        m_customModel->appendRow(rowFor(language));
}

QList<QStandardItem*> LanguageDialog::rowFor(const CustomLanguage& language) const
{
    return {new QStandardItem(language.name), new QStandardItem(language.key)};
}

QVector<CustomLanguage> LanguageDialog::customLanguages() const
{
    QVector<CustomLanguage> result;
    result.reserve(m_customModel->rowCount());
    for (int row = 0; row < m_customModel->rowCount(); ++row) {
        CustomLanguage language;
        if (const QStandardItem* key = m_customModel->item(row, kKeyColumn))
            language.key = key->text();
        if (const QStandardItem* name = m_customModel->item(row, kNameColumn))
            language.name = name->text();
        result.append(language);
    }
    return Languages::customNormalized(result);
}

int LanguageDialog::currentRow() const
{
    const QModelIndex index = m_custom->currentIndex();
    return index.isValid() ? index.row() : -1;
}

void LanguageDialog::refreshButtons()
{
    const int row = currentRow();
    const bool hasSelection = row >= 0;
    m_removeButton->setEnabled(hasSelection);
    m_moveUpButton->setEnabled(hasSelection && row > 0);
    m_moveDownButton->setEnabled(hasSelection && row < m_customModel->rowCount() - 1);
}

void LanguageDialog::addLanguage()
{
    const int row = m_customModel->rowCount();
    m_customModel->appendRow(rowFor(CustomLanguage{}));
    const QModelIndex index = m_customModel->index(row, kNameColumn);
    m_custom->setCurrentIndex(index);
    m_custom->edit(index);
}

void LanguageDialog::removeLanguage()
{
    const int row = currentRow();
    if (row < 0)
        return;
    m_customModel->removeRow(row);
    refreshButtons();
}

void LanguageDialog::moveLanguage(int offset)
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
