#include "ApiPresetDialog.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
// Role holding the captured API fields of the preset named by the item text.
constexpr int kValuesRole = Qt::UserRole;
}

ApiPresetDialog::ApiPresetDialog(const QVector<ApiPreset>& presets, int selectedIndex, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Manage API presets"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Double-click a preset to load it."), this));

    // The list owns the presets: the item text is the name and its data is the
    // captured settings, so every operation works on one source of truth.
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    for (const ApiPreset& preset : presets) {
        auto* item = new QListWidgetItem(preset.name, m_list);
        item->setData(kValuesRole, preset.values);
    }
    // The preset in effect is highlighted; settings matching none fall back to
    // the first entry so the window always opens with a usable selection.
    if (m_list->count() > 0)
        m_list->setCurrentRow(selectedIndex >= 0 && selectedIndex < m_list->count() ? selectedIndex : 0);
    layout->addWidget(m_list, 1);

    auto* buttonRow = new QHBoxLayout();
    m_moveUpButton = new QPushButton(tr("Move up"), this);
    m_moveDownButton = new QPushButton(tr("Move down"), this);
    m_renameButton = new QPushButton(tr("Rename"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    m_loadButton = new QPushButton(tr("Load"), this);
    buttonRow->addWidget(m_moveUpButton);
    buttonRow->addWidget(m_moveDownButton);
    buttonRow->addWidget(m_renameButton);
    buttonRow->addWidget(m_removeButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_loadButton);
    layout->addLayout(buttonRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(m_moveUpButton, &QPushButton::clicked, this, [this]() { moveSelected(-1); });
    connect(m_moveDownButton, &QPushButton::clicked, this, [this]() { moveSelected(1); });
    connect(m_renameButton, &QPushButton::clicked, this, &ApiPresetDialog::renameSelected);
    connect(m_removeButton, &QPushButton::clicked, this, &ApiPresetDialog::removeSelected);
    connect(m_loadButton, &QPushButton::clicked, this, &ApiPresetDialog::loadSelected);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &ApiPresetDialog::refreshButtons);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { loadSelected(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshButtons();
}

void ApiPresetDialog::manage(QWidget* parent)
{
    ConfigManager* config = ConfigManager::instance();
    const QVector<ApiPreset> presets = ApiPresets::fromJson(config->value(Keys::apiPresets).toArray());
    // No pending edits here: the applied configuration decides the selection.
    ApiPresetDialog dialog(presets, ApiPresets::matchValues(presets, ApiPresets::capture()), parent);
    if (dialog.exec() != QDialog::Accepted)
        return;

    config->setValue(Keys::apiPresets, ApiPresets::toJson(dialog.presets()));
    const QVector<ApiPreset> edited = dialog.presets();
    const int loaded = dialog.loadedIndex();
    if (loaded >= 0 && loaded < edited.size())
        ApiPresets::apply(edited.at(loaded));
}

QVector<ApiPreset> ApiPresetDialog::presets() const
{
    QVector<ApiPreset> result;
    result.reserve(m_list->count());
    for (int i = 0; i < m_list->count(); ++i) {
        const QListWidgetItem* item = m_list->item(i);
        ApiPreset preset;
        preset.name = item->text();
        preset.values = item->data(kValuesRole).toJsonObject();
        result.append(preset);
    }
    return result;
}

void ApiPresetDialog::refreshButtons()
{
    const int row = m_list->currentRow();
    const bool hasSelection = row >= 0;
    m_moveUpButton->setEnabled(hasSelection && row > 0);
    m_moveDownButton->setEnabled(hasSelection && row < m_list->count() - 1);
    m_renameButton->setEnabled(hasSelection);
    m_removeButton->setEnabled(hasSelection);
    m_loadButton->setEnabled(hasSelection);
}

void ApiPresetDialog::renameSelected()
{
    QListWidgetItem* item = m_list->item(m_list->currentRow());
    if (!item)
        return;

    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Rename preset"), tr("Preset name"),
                                              QLineEdit::Normal, item->text(), &accepted).trimmed();
    if (!accepted || name.isEmpty())
        return;

    // Names must stay unique, since a preset is identified by its name.
    const QList<QListWidgetItem*> clashes = m_list->findItems(name, Qt::MatchExactly);
    if (!clashes.isEmpty() && clashes.first() != item) {
        QMessageBox::warning(this, tr("RiipL"), tr("A preset named \"%1\" already exists.").arg(name));
        return;
    }

    item->setText(name);
}

void ApiPresetDialog::removeSelected()
{
    const int row = m_list->currentRow();
    QListWidgetItem* item = m_list->item(row);
    if (!item)
        return;

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Remove preset"), tr("Remove the preset \"%1\"?").arg(item->text()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    delete m_list->takeItem(row);
    if (m_list->count() > 0)
        m_list->setCurrentRow(qMin(row, m_list->count() - 1));
    refreshButtons();
}

void ApiPresetDialog::moveSelected(int offset)
{
    const int row = m_list->currentRow();
    const int target = row + offset;
    if (!m_list->item(row) || target < 0 || target >= m_list->count())
        return;

    // Re-inserting the existing item moves it with its data and item state.
    m_list->insertItem(target, m_list->takeItem(row));
    m_list->setCurrentRow(target);
    refreshButtons();
}

void ApiPresetDialog::loadSelected()
{
    if (!m_list->item(m_list->currentRow()))
        return;

    m_loadedIndex = m_list->currentRow();
    accept();
}
