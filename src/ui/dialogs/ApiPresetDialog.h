#pragma once

#include <QDialog>
#include <QVector>

#include "core/config/ApiPreset.h"

class QListWidget;
class QPushButton;
class QToolButton;

// Manages the named API presets and loads the highlighted one on accept. Presets
// themselves are created on the API settings page, which knows the live values.
class ApiPresetDialog : public QDialog
{
    Q_OBJECT

public:
    // \p selectedIndex is highlighted on entry, falling back to the first preset.
    explicit ApiPresetDialog(const QVector<ApiPreset>& presets, int selectedIndex = -1,
                             QWidget* parent = nullptr);

    void accept() override;

    // Runs the window against the stored presets for callers that hold no pending
    // API edits of their own.
    static void manage(QWidget* parent);

    // Presets as edited, in display order.
    QVector<ApiPreset> presets() const;
    // Preset loaded on accept, or -1 for an empty list.
    int loadedIndex() const { return m_loadedIndex; }

private slots:
    void renameSelected();
    void copySelected();
    void removeSelected();
    void moveSelected(int offset);
    void refreshButtons();

private:
    QString uniqueName(const QString& base) const;

    int m_loadedIndex = -1;
    QListWidget* m_list = nullptr;
    QPushButton* m_renameButton = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QToolButton* m_moveUpButton = nullptr;
    QToolButton* m_moveDownButton = nullptr;
};
