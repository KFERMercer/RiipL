#pragma once

#include <QDialog>
#include <QJsonArray>

class QPushButton;
class ConfigCheckBox;
class ConfigComboBox;
class GlossaryTable;

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
    explicit SettingsDialog(QWidget* parent = nullptr);

    void reject() override;

private slots:
    void updateDirtyState();
    void applyChanges();

private:
    QWidget* createApiPage();
    QWidget* createTranslationPage();
    QWidget* createGlossaryPage();
    QWidget* createPromptsPage();
    QWidget* createInterfacePage();
    QWidget* createClipboardPage();
    QWidget* createHistoryPage();

    bool isDirty() const;

    ConfigComboBox* m_targetLangCombo = nullptr;
    ConfigComboBox* m_toneCombo = nullptr;
    ConfigCheckBox* m_glossaryEnabled = nullptr;
    GlossaryTable* m_glossaryTable = nullptr;
    QJsonArray m_customTones;
    QPushButton* m_applyButton = nullptr;
};
