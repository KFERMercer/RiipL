#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

#include <functional>

#include "core/config/Preset.h"

class ConfigTextEdit;
class PresetListEditor;
class QPushButton;

// Manages the named context presets and edits the style and background a
// translation runs with. Apply commits without closing the window; what is left
// unapplied is dropped when the dialog is rejected.
class ContextPresetDialog : public QDialog
{
    Q_OBJECT

public:
    // \p presets are managed in place, \p values is the style and background the
    // editors start from, and \p selectedIndex is highlighted on entry.
    explicit ContextPresetDialog(const QVector<Preset>& presets, const QJsonObject& values,
                                 int selectedIndex = -1, QWidget* parent = nullptr);

    void reject() override;

    // Runs the window against the stored presets and the applied context, for
    // callers that hold no pending edits of their own.
    static void manage(QWidget* parent);

    // Takes over what Apply commits, for a caller whose own edits stay pending;
    // without one the configuration is written directly.
    void setApplyHandler(std::function<void(const QVector<Preset>&, const QJsonObject&)> handler);

    // Presets as edited, in display order.
    QVector<Preset> presets() const;
    QJsonObject values() const;

private slots:
    void applyChanges();
    void loadSelectedPreset();
    void startPreset();
    void savePreset();
    void saveAsPreset();
    void refreshButtons();

private:
    // True while the presets or the context differ from what Apply last committed.
    bool isDirty() const;
    // Fills the editors from \p preset; a field it does not carry falls back to
    // its default.
    void applyValues(const Preset& preset);

    PresetListEditor* m_list = nullptr;
    ConfigTextEdit* m_styleEdit = nullptr;
    ConfigTextEdit* m_backgroundEdit = nullptr;
    QPushButton* m_applyButton = nullptr;
    QPushButton* m_newButton = nullptr;
    QPushButton* m_saveButton = nullptr;
    QPushButton* m_saveAsButton = nullptr;
    // Presets as they stood when Apply last committed. A preset carries no
    // comparison of its own, so both sides are held serialized, and the current
    // side is rebuilt only when the list itself changes.
    QJsonArray m_appliedPresets;
    QJsonArray m_currentPresets;
    QJsonObject m_appliedValues;
    std::function<void(const QVector<Preset>&, const QJsonObject&)> m_apply;
};
