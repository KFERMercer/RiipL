#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

#include "core/config/ApiPreset.h"

class QComboBox;
class QPushButton;
class ConfigCheckBox;
class ConfigComboBox;
class HistoryManager;

// Form-style settings dialog following Qt's canonical pattern: editors are
// populated once on construction and nothing is written back until the user
// activates Apply (or OK). Dirty state is derived precisely by comparing
// every editor against its loaded baseline, so reverting an edit disables
// Apply again. Cancel discards pending edits, asking for confirmation when
// any change is pending.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(HistoryManager* history, QWidget* parent = nullptr);

    void reject() override;

private slots:
    void updateDirtyState();
    void applyChanges();
    void reloadPresets();
    void applySelectedPreset(int index);
    void savePreset();
    void managePresets();

private:
    QWidget* createApiPage();
    QWidget* createTranslationPage();
    QWidget* createInterfacePage();
    QWidget* createClipboardPage();
    QWidget* createHistoryPage();
    QWidget* createPromptsPage();

    bool isDirty() const;
    QJsonObject editedApiValues() const;
    void applyPresetValues(const ApiPreset& preset);

    ConfigComboBox* m_targetLangCombo = nullptr;
    ConfigComboBox* m_toneCombo = nullptr;
    ConfigCheckBox* m_glossaryEnabled = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QJsonArray m_customTones;
    QVector<ApiPreset> m_apiPresets;
    QPushButton* m_applyButton = nullptr;
    HistoryManager* m_history = nullptr;
};
