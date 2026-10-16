#pragma once

#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>
#include <optional>

#include "core/config/Preset.h"

class QComboBox;
class QPushButton;

// Selects, creates and manages named presets over one group of configuration
// fields. The owning page keeps the editors, so the bar reads their pending
// values through a callback and hands back the preset to load.
class PresetBar : public QWidget
{
    Q_OBJECT

public:
    // What the manage dialog returned: the edited list and the preset to load.
    struct Managed
    {
        QVector<Preset> presets;
        std::optional<Preset> applied = std::nullopt;
    };

    // \p presetKey is the configuration key holding the presets, and \p fields
    // are the keys a preset captures.
    explicit PresetBar(const QString& presetKey, const QStringList& fields,
                       QWidget* parent = nullptr);

    // Pending values of the page's editors, recorded by Save and Save as...
    void setValuesProvider(std::function<QJsonObject()> provider);
    // Fills the page's editors from \p preset.
    void setApplier(std::function<void(const Preset&)> applier);
    // Runs Manage...; a nullopt result leaves the presets untouched.
    void setManageHandler(std::function<std::optional<Managed>(const QVector<Preset>&, int)> handler);

    QVector<Preset> presets() const;
    // Replaces the presets and points the selector at the one they now match.
    void setPresets(const QVector<Preset>& presets);

    // Reads the stored presets and points the selector at the preset the current
    // values match.
    void loadPresets();
    // True while the presets differ from the stored ones.
    bool isDirty() const;
    // Writes the presets into the configuration when they differ.
    void applyPresets();

    // Rebuilds the selector and points it at the preset the current values match.
    void syncSelection();
    // Enables the actions that would change something.
    void refreshActions();
    void retranslateUi();

signals:
    // The preset list or the selection changed, so the page holds a pending edit.
    void changed();

private slots:
    void applySelected(int index);
    void startPreset();
    void overwritePreset();
    void savePreset();
    void managePresets();

private:
    QJsonObject currentValues() const;
    int matchedPreset() const;
    void refresh();

    QString m_presetKey;
    QStringList m_fields;
    QVector<Preset> m_presets;
    int m_selectedPreset = -1;
    QComboBox* m_combo = nullptr;
    QPushButton* m_newButton = nullptr;
    QPushButton* m_overwriteButton = nullptr;
    QPushButton* m_saveAsButton = nullptr;
    QPushButton* m_manageButton = nullptr;
    std::function<QJsonObject()> m_values;
    std::function<void(const Preset&)> m_applier;
    std::function<std::optional<Managed>(const QVector<Preset>&, int)> m_manage;
};
