#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QVector>

#include "core/config/Preset.h"

class PresetListEditor;

// Manages the named API presets and loads the highlighted one on accept. Presets
// themselves are created on the API settings page, which knows the live values.
class ApiPresetDialog : public QDialog
{
    Q_OBJECT

public:
    // \p selectedIndex is highlighted on entry, falling back to the first preset.
    explicit ApiPresetDialog(const QVector<Preset>& presets, int selectedIndex = -1,
                             QWidget* parent = nullptr);

    void reject() override;

    // Runs the window against the stored presets for callers that hold no pending
    // API edits of their own.
    static void manage(QWidget* parent);

    // Presets as edited, in display order.
    QVector<Preset> presets() const;
    // Preset loaded on accept, or -1 for an empty list.
    int loadedIndex() const;

private:
    // True while the presets differ from the ones the window opened with.
    bool isDirty() const;

    PresetListEditor* m_list = nullptr;
    QJsonArray m_initialPresets;
};
