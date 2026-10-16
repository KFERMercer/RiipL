#include "ApiPresetDialog.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "ui/widgets/PresetListEditor.h"
#include "ui/widgets/WindowState.h"

#include <QDialogButtonBox>
#include <QVBoxLayout>

ApiPresetDialog::ApiPresetDialog(const QVector<Preset>& presets, int selectedIndex, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("API presets"));

    auto* layout = new QVBoxLayout(this);
    m_list = new PresetListEditor({}, {}, this);
    layout->addWidget(m_list, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(m_list, &PresetListEditor::loadRequested, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_list->setPresets(presets);
    // Fall back to the first preset so the window opens with a usable selection.
    if (!presets.isEmpty())
        m_list->setSelectedIndex(selectedIndex >= 0 && selectedIndex < presets.size() ? selectedIndex : 0);
    WindowState::track(this, WindowState::Id::apiPresets);
}

void ApiPresetDialog::manage(QWidget* parent)
{
    ConfigManager* config = ConfigManager::instance();
    const QStringList fields = Keys::apiPresetFields();
    const QVector<Preset> presets = Presets::fromJson(config->value(Keys::apiPresets).toArray());
    // No pending edits here: the applied configuration decides the selection.
    ApiPresetDialog dialog(presets, Presets::matchValues(presets, Presets::capture(fields), fields),
                           parent);
    if (dialog.exec() != QDialog::Accepted)
        return;

    config->setValue(Keys::apiPresets, Presets::toJson(dialog.presets()));
    const QVector<Preset> edited = dialog.presets();
    const int loaded = dialog.loadedIndex();
    if (loaded >= 0 && loaded < edited.size())
        Presets::apply(edited.at(loaded), fields);
}

QVector<Preset> ApiPresetDialog::presets() const
{
    return m_list->presets();
}

int ApiPresetDialog::loadedIndex() const
{
    return m_list->selectedIndex();
}
