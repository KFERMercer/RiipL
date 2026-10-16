#include "PresetBar.h"

#include "core/config/ConfigManager.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>

#include <utility>

PresetBar::PresetBar(const QString& presetKey, const QStringList& fields, QWidget* parent)
    : QWidget(parent)
    , m_presetKey(presetKey)
    , m_fields(fields)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_combo = new QComboBox(this);
    m_newButton = new QPushButton(this);
    m_overwriteButton = new QPushButton(this);
    m_saveAsButton = new QPushButton(this);
    m_manageButton = new QPushButton(this);
    layout->addWidget(m_combo, 1);
    layout->addWidget(m_newButton);
    layout->addWidget(m_overwriteButton);
    layout->addWidget(m_saveAsButton);
    layout->addWidget(m_manageButton);

    connect(m_combo, &QComboBox::activated, this, &PresetBar::applySelected);
    connect(m_newButton, &QPushButton::clicked, this, &PresetBar::startPreset);
    connect(m_overwriteButton, &QPushButton::clicked, this, &PresetBar::overwritePreset);
    connect(m_saveAsButton, &QPushButton::clicked, this, &PresetBar::savePreset);
    connect(m_manageButton, &QPushButton::clicked, this, &PresetBar::managePresets);

    retranslateUi();
    refreshActions();
}

void PresetBar::setValuesProvider(std::function<QJsonObject()> provider)
{
    m_values = std::move(provider);
}

void PresetBar::setApplier(std::function<void(const Preset&)> applier)
{
    m_applier = std::move(applier);
}

void PresetBar::setManageHandler(
    std::function<std::optional<Managed>(const QVector<Preset>&, int)> handler)
{
    m_manage = std::move(handler);
}

QVector<Preset> PresetBar::presets() const
{
    return m_presets;
}

void PresetBar::setPresets(const QVector<Preset>& presets)
{
    m_presets = presets;
    syncSelection();
}

void PresetBar::loadPresets()
{
    setPresets(Presets::fromJson(ConfigManager::instance()->value(m_presetKey).toArray()));
}

bool PresetBar::isDirty() const
{
    return QJsonValue(Presets::toJson(m_presets)) != ConfigManager::instance()->value(m_presetKey);
}

void PresetBar::applyPresets()
{
    if (isDirty())
        ConfigManager::instance()->setValue(m_presetKey, Presets::toJson(m_presets));
}

void PresetBar::syncSelection()
{
    m_selectedPreset = matchedPreset();
    refresh();
}

void PresetBar::retranslateUi()
{
    m_combo->setPlaceholderText(tr("Empty preset"));
    m_newButton->setText(tr("New"));
    m_newButton->setToolTip(tr("Create an empty preset"));
    m_newButton->setAccessibleName(tr("New preset"));
    m_overwriteButton->setText(tr("Save preset"));
    m_saveAsButton->setText(tr("Save as..."));
    m_saveAsButton->setToolTip(tr("Save the current values under a new name"));
    m_manageButton->setText(tr("Manage..."));
    m_manageButton->setToolTip(tr("Rename, copy, reorder, delete or load presets"));
}

void PresetBar::refreshActions()
{
    const QJsonObject current = currentValues();
    const bool held = m_selectedPreset >= 0 && m_selectedPreset < m_presets.size();
    m_overwriteButton->setEnabled(
        held ? current != Presets::withDefaults(m_presets.at(m_selectedPreset).values, m_fields)
             : current != Presets::withDefaults(Presets::capture(m_fields), m_fields));
    m_overwriteButton->setToolTip(held
        ? tr("Overwrite \"%1\" with the current values").arg(m_presets.at(m_selectedPreset).name)
        : tr("Save the current values as a new preset"));
}

void PresetBar::applySelected(int index)
{
    if (index < 0 || index >= m_presets.size())
        return;
    m_selectedPreset = index;
    if (m_applier)
        m_applier(m_presets.at(index));
    refreshActions();
    emit changed();
}

void PresetBar::startPreset()
{
    if (currentValues() != Presets::withDefaults(Presets::capture(m_fields), m_fields)) {
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

    if (m_applier)
        m_applier(Preset{});
    m_selectedPreset = -1;
    refresh();
    emit changed();
}

void PresetBar::overwritePreset()
{
    if (m_selectedPreset < 0 || m_selectedPreset >= m_presets.size()) {
        savePreset();
        return;
    }

    m_presets[m_selectedPreset].values = currentValues();
    refresh();
    emit changed();
}

void PresetBar::savePreset()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Save preset"), tr("Preset name"),
                                              QLineEdit::Normal, m_combo->currentText(),
                                              &accepted).trimmed();
    if (!accepted || name.isEmpty())
        return;

    const Preset preset{name, currentValues()};
    const int existing = Presets::indexOf(m_presets, name);
    if (existing >= 0) {
        m_presets[existing] = preset;
        m_selectedPreset = existing;
    } else {
        m_presets.append(preset);
        m_selectedPreset = m_presets.size() - 1;
    }
    refresh();
    emit changed();
}

void PresetBar::managePresets()
{
    if (!m_manage)
        return;
    const std::optional<Managed> result = m_manage(m_presets, m_selectedPreset);
    if (!result)
        return;

    m_presets = result->presets;
    if (result->applied) {
        if (m_applier)
            m_applier(*result->applied);
        // A loaded preset is claimed by name, so duplicates of its values do not
        // take the selection over.
        m_selectedPreset = result->applied->name.isEmpty()
            ? matchedPreset()
            : Presets::indexOf(m_presets, result->applied->name);
    }
    refresh();
    emit changed();
}

QJsonObject PresetBar::currentValues() const
{
    return Presets::withDefaults(m_values ? m_values() : QJsonObject(), m_fields);
}

int PresetBar::matchedPreset() const
{
    return Presets::matchValues(m_presets, currentValues(), m_fields);
}

void PresetBar::refresh()
{
    const QSignalBlocker blocker(m_combo);
    m_combo->clear();
    for (const Preset& preset : std::as_const(m_presets))
        m_combo->addItem(preset.name);
    const bool held = m_selectedPreset >= 0 && m_selectedPreset < m_presets.size();
    m_combo->setCurrentIndex(held ? m_selectedPreset : -1);
    refreshActions();
}
