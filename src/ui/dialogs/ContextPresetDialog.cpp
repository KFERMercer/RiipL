#include "ContextPresetDialog.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "ui/widgets/ConfigEditors.h"
#include "ui/widgets/PresetListEditor.h"
#include "ui/widgets/WindowState.h"

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

ContextPresetDialog::ContextPresetDialog(const QVector<Preset>& presets, const QJsonObject& values,
                                         int selectedIndex, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Context presets"));

    auto* layout = new QVBoxLayout(this);

    auto* presetGroup = new QGroupBox(tr("Presets"), this);
    auto* groupLayout = new QVBoxLayout(presetGroup);
    const QVector<PresetListEditor::FieldColumn> columns = {
        {Keys::translationStyle, tr("Style")},
        {Keys::translationBackground, tr("Background")},
    };
    m_list = new PresetListEditor(columns, {true, true}, presetGroup);
    groupLayout->addWidget(m_list);
    layout->addWidget(presetGroup, 1);

    auto* contextFrame = new QFrame(this);
    contextFrame->setFrameShape(QFrame::StyledPanel);
    auto* contextLayout = new QVBoxLayout(contextFrame);

    auto* saveRow = new QHBoxLayout();
    m_newButton = new QPushButton(tr("New"), contextFrame);
    m_newButton->setToolTip(tr("Create an empty preset"));
    m_newButton->setAccessibleName(tr("New preset"));
    m_saveButton = new QPushButton(tr("Save preset"), contextFrame);
    m_saveAsButton = new QPushButton(tr("Save as..."), contextFrame);
    m_saveAsButton->setToolTip(tr("Save the current values under a new name"));
    saveRow->addStretch(1);
    saveRow->addWidget(m_newButton);
    saveRow->addWidget(m_saveButton);
    saveRow->addWidget(m_saveAsButton);
    saveRow->addStretch(1);
    contextLayout->addLayout(saveRow);

    // The editors hold the context the next translation runs with, so the
    // caller's pending values seed them.
    const QJsonObject context = Presets::withDefaults(values, Keys::contextPresetFields());
    auto* form = new QFormLayout();
    m_styleEdit = new ConfigTextEdit(Keys::translationStyle, contextFrame);
    m_styleEdit->setUserValue(context.value(Keys::translationStyle));
    m_backgroundEdit = new ConfigTextEdit(Keys::translationBackground, contextFrame);
    m_backgroundEdit->setUserValue(context.value(Keys::translationBackground));
    form->addRow(tr("Style"), m_styleEdit);
    form->addRow(tr("Background"), m_backgroundEdit);
    contextLayout->addLayout(form);
    layout->addWidget(contextFrame);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply
                                         | QDialogButtonBox::Cancel, this);
    m_applyButton = buttons->button(QDialogButtonBox::Apply);
    layout->addWidget(buttons);

    connect(m_list, &PresetListEditor::loadRequested, this, &ContextPresetDialog::loadSelectedPreset);
    connect(m_list, &PresetListEditor::changed, this, [this]() {
        m_currentPresets = Presets::toJson(m_list->presets());
        refreshButtons();
    });
    for (ConfigTextEdit* editor : findChildren<ConfigTextEdit*>())
        connect(editor, &ConfigEditor::edited, this, &ContextPresetDialog::refreshButtons);
    connect(m_newButton, &QPushButton::clicked, this, &ContextPresetDialog::startPreset);
    connect(m_saveButton, &QPushButton::clicked, this, &ContextPresetDialog::savePreset);
    connect(m_saveAsButton, &QPushButton::clicked, this, &ContextPresetDialog::saveAsPreset);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::clicked, this,
            [this, buttons](QAbstractButton* button) {
                if (buttons->buttonRole(button) == QDialogButtonBox::ApplyRole)
                    applyChanges();
            });

    m_list->setPresets(presets);
    // Fall back to the first preset so the window opens with a usable selection.
    if (!presets.isEmpty())
        m_list->setSelectedIndex(selectedIndex >= 0 && selectedIndex < presets.size() ? selectedIndex : 0);

    m_appliedPresets = m_currentPresets;
    m_appliedValues = Presets::withDefaults(this->values(), Keys::contextPresetFields());
    refreshButtons();
    WindowState::track(this, WindowState::Id::contextPresets);
}

// Cancel discards pending edits, so it asks first while any are held.
void ContextPresetDialog::reject()
{
    if (isDirty()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Unsaved changes"));
        box.setText(tr("Your changes have not been applied yet."));
        box.setStandardButtons(QMessageBox::Discard | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() != QMessageBox::Discard)
            return;
    }

    QDialog::reject();
}

void ContextPresetDialog::manage(QWidget* parent)
{
    ConfigManager* config = ConfigManager::instance();
    const QStringList fields = Keys::contextPresetFields();
    const QVector<Preset> presets = Presets::fromJson(config->value(Keys::contextPresets).toArray());
    const QJsonObject values = Presets::withDefaults(Presets::capture(fields), fields);
    ContextPresetDialog dialog(presets, values, Presets::matchValues(presets, values, fields), parent);
    if (dialog.exec() != QDialog::Accepted)
        return;

    config->setValue(Keys::contextPresets, Presets::toJson(dialog.presets()));
    Presets::apply(Preset{QString(), dialog.values()}, fields);
}

void ContextPresetDialog::setApplyHandler(
    std::function<void(const QVector<Preset>&, const QJsonObject&)> handler)
{
    m_apply = std::move(handler);
}

QVector<Preset> ContextPresetDialog::presets() const
{
    return m_list->presets();
}

QJsonObject ContextPresetDialog::values() const
{
    return QJsonObject{{Keys::translationStyle, m_styleEdit->value()},
                       {Keys::translationBackground, m_backgroundEdit->value()}};
}

void ContextPresetDialog::applyChanges()
{
    if (m_apply) {
        m_apply(presets(), values());
    } else {
        ConfigManager* config = ConfigManager::instance();
        config->setValue(Keys::contextPresets, Presets::toJson(presets()));
        Presets::apply(Preset{QString(), values()}, Keys::contextPresetFields());
    }

    m_appliedPresets = m_currentPresets;
    m_appliedValues = Presets::withDefaults(this->values(), Keys::contextPresetFields());
    refreshButtons();
}

bool ContextPresetDialog::isDirty() const
{
    if (m_currentPresets != m_appliedPresets)
        return true;
    return Presets::withDefaults(values(), Keys::contextPresetFields()) != m_appliedValues;
}

void ContextPresetDialog::refreshButtons()
{
    const int row = m_list->selectedIndex();
    const QJsonObject current = Presets::withDefaults(values(), Keys::contextPresetFields());
    const QJsonObject stored = row >= 0 ? m_list->selectedValues()
                                        : Presets::capture(Keys::contextPresetFields());
    m_saveButton->setEnabled(current != Presets::withDefaults(stored, Keys::contextPresetFields()));
    m_applyButton->setEnabled(isDirty());
}

// Loading a preset replaces the context the translation would run with, so the
// current values are only dropped once the user confirms it.
void ContextPresetDialog::loadSelectedPreset()
{
    if (m_list->selectedIndex() < 0)
        return;

    const QJsonObject values = m_list->selectedValues();
    const bool differs = values.value(Keys::translationStyle) != m_styleEdit->value()
        || values.value(Keys::translationBackground) != m_backgroundEdit->value();
    if (differs) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("Load preset"));
        box.setText(tr("Replace the current style and background with the preset \"%1\"?")
                        .arg(m_list->selectedName()));
        box.setStandardButtons(QMessageBox::Apply | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() != QMessageBox::Apply)
            return;
    }

    applyValues(Preset{QString(), values});
    refreshButtons();
}

// Starts a preset from the default fields, named later by a save action.
void ContextPresetDialog::startPreset()
{
    const QJsonObject applied = Presets::withDefaults(Presets::capture(Keys::contextPresetFields()),
                                                      Keys::contextPresetFields());
    if (Presets::withDefaults(values(), Keys::contextPresetFields()) != applied) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("New preset"));
        box.setText(tr("The fields hold changes that have not been applied yet."));
        box.setInformativeText(tr("Discard them and start from the defaults?"));
        box.setStandardButtons(QMessageBox::Discard | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() != QMessageBox::Discard)
            return;
    }

    applyValues(Preset{});
    m_list->setSelectedIndex(-1);
    refreshButtons();
}

// Writes the pending fields into the selected preset. With nothing selected
// there is no preset to overwrite yet, so the values are named instead.
void ContextPresetDialog::savePreset()
{
    if (m_list->selectedIndex() < 0) {
        saveAsPreset();
        return;
    }

    m_list->overwriteSelected(values());
    refreshButtons();
}

// Adds a preset under a name the user picks, replacing the entry when the name
// is taken.
void ContextPresetDialog::saveAsPreset()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Save preset"), tr("Preset name"),
                                              QLineEdit::Normal, m_list->selectedName(),
                                              &accepted).trimmed();
    if (!accepted || name.isEmpty())
        return;

    m_list->saveAs(name, values());
    refreshButtons();
}

void ContextPresetDialog::applyValues(const Preset& preset)
{
    const QJsonObject values = Presets::withDefaults(preset.values, Keys::contextPresetFields());
    m_styleEdit->setUserValue(values.value(Keys::translationStyle));
    m_backgroundEdit->setUserValue(values.value(Keys::translationBackground));
}
