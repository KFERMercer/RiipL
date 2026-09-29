#include "ApiPresetDialog.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "ui/widgets/AppIcons.h"
#include "ui/widgets/WindowState.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
// Captured API fields of the item's preset.
constexpr int kValuesRole = Qt::UserRole;
}

ApiPresetDialog::ApiPresetDialog(const QVector<ApiPreset>& presets, int selectedIndex, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("API presets"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("OK loads the highlighted preset."), this));

    // The item text is the name and its data holds the captured settings.
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    for (const ApiPreset& preset : presets) {
        auto* item = new QListWidgetItem(preset.name, m_list);
        item->setData(kValuesRole, preset.values);
    }
    // Fall back to the first preset so the window opens with a usable selection.
    if (m_list->count() > 0)
        m_list->setCurrentRow(selectedIndex >= 0 && selectedIndex < m_list->count() ? selectedIndex : 0);
    layout->addWidget(m_list, 1);

    auto* buttonRow = new QHBoxLayout();
    m_moveUpButton = new QToolButton(this);
    m_moveUpButton->setIcon(AppIcons::moveUp());
    m_moveUpButton->setToolTip(tr("Move up"));
    m_moveDownButton = new QToolButton(this);
    m_moveDownButton->setIcon(AppIcons::moveDown());
    m_moveDownButton->setToolTip(tr("Move down"));
    m_renameButton = new QPushButton(tr("Rename"), this);
    m_copyButton = new QPushButton(tr("Copy"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    buttonRow->addWidget(m_moveUpButton);
    buttonRow->addWidget(m_moveDownButton);
    buttonRow->addWidget(m_renameButton);
    buttonRow->addWidget(m_copyButton);
    buttonRow->addWidget(m_removeButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(m_moveUpButton, &QToolButton::clicked, this, [this]() { moveSelected(-1); });
    connect(m_moveDownButton, &QToolButton::clicked, this, [this]() { moveSelected(1); });
    connect(m_renameButton, &QPushButton::clicked, this, &ApiPresetDialog::renameSelected);
    connect(m_copyButton, &QPushButton::clicked, this, &ApiPresetDialog::copySelected);
    connect(m_removeButton, &QPushButton::clicked, this, &ApiPresetDialog::removeSelected);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &ApiPresetDialog::refreshButtons);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { accept(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &ApiPresetDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshButtons();
    WindowState::track(this, WindowState::Id::apiPresets);
}

void ApiPresetDialog::accept()
{
    m_loadedIndex = m_list->currentRow();
    QDialog::accept();
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
    m_copyButton->setEnabled(hasSelection);
    m_removeButton->setEnabled(hasSelection);
}

QString ApiPresetDialog::uniqueName(const QString& base) const
{
    // A recognised copy marker keeps duplicates of duplicates on one stem; the
    // marker is translated, hence the escape.
    const QRegularExpression suffix(
        QStringLiteral("\\s+%1(?:\\s+(\\d+))?$").arg(QRegularExpression::escape(tr("copy"))));
    const QRegularExpressionMatch match = suffix.match(base);
    const QString stem = match.hasMatch() ? base.left(match.capturedStart()).trimmed() : base;

    const auto taken = [this](const QString& name) {
        return !m_list->findItems(name, Qt::MatchExactly).isEmpty();
    };
    const QString firstCopy = tr("%1 copy").arg(stem);
    if (!match.hasMatch() && !taken(firstCopy))
        return firstCopy;

    int number = match.captured(1).isEmpty() ? 1 : match.captured(1).toInt();
    for (;;) {
        const QString candidate = tr("%1 copy %2").arg(stem).arg(++number);
        if (!taken(candidate))
            return candidate;
    }
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

void ApiPresetDialog::copySelected()
{
    QListWidgetItem* item = m_list->item(m_list->currentRow());
    if (!item)
        return;

    auto* copy = new QListWidgetItem(uniqueName(item->text()));
    m_list->insertItem(m_list->row(item) + 1, copy);
    copy->setData(kValuesRole, item->data(kValuesRole));
    m_list->setCurrentItem(copy);
    refreshButtons();
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
    if (row < 0)
        return;
    const int target = row + offset;
    if (target < 0 || target >= m_list->count())
        return;
    // destinationChild is the index the row lands before, so a downward move needs one step more.
    m_list->model()->moveRows(QModelIndex(), row, 1, QModelIndex(), offset < 0 ? target : target + 1);
    m_list->setCurrentRow(target);
    // moveRows keeps the current item, so the states are refreshed here.
    refreshButtons();
}
