#pragma once

#include <QDialog>
#include <QVector>

#include "core/config/ApiPreset.h"

class QListWidget;
class QPushButton;

// Manages the named API presets: rename, reorder, delete, and load the
// selected preset into the running configuration. Presets themselves are
// created from the API settings page, which knows the live field values.
class ApiPresetDialog : public QDialog
{
    Q_OBJECT

public:
    // \p selectedIndex is highlighted on entry, or the first preset when it is
    // out of range because the settings match no stored preset.
    explicit ApiPresetDialog(const QVector<ApiPreset>& presets, int selectedIndex = -1,
                             QWidget* parent = nullptr);

    // Runs the window against the stored presets, persisting any edits and
    // applying the preset the user asked to load. For callers that hold no
    // pending API edits of their own.
    static void manage(QWidget* parent);

    // Presets as edited, in display order.
    QVector<ApiPreset> presets() const;
    // Preset the user asked to load, or -1 when the dialog was simply closed.
    int loadedIndex() const { return m_loadedIndex; }

private slots:
    void renameSelected();
    void removeSelected();
    void moveSelected(int offset);
    void loadSelected();
    void refreshButtons();

private:
    int m_loadedIndex = -1;
    QListWidget* m_list = nullptr;
    QPushButton* m_renameButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_loadButton = nullptr;
    QPushButton* m_moveUpButton = nullptr;
    QPushButton* m_moveDownButton = nullptr;
};