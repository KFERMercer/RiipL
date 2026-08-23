#pragma once

#include <QDialog>
#include <QJsonArray>

class QKeySequenceEdit;
class QPushButton;
class ConfigCheckBox;
class ConfigComboBox;
class GlossaryTable;

class GlobalHotkey;

// Form-style settings dialog following Qt's canonical pattern: editors are
// populated once on construction and nothing is written back until the user
// activates Apply (or OK). Cancel simply discards pending edits.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(GlobalHotkey* hotkey, int initialTab = 0, QWidget* parent = nullptr);

private slots:
    void markDirty();
    void applyChanges();

private:
    QWidget* createApiPage();
    QWidget* createTranslationPage();
    QWidget* createGlossaryPage();
    QWidget* createPromptsPage();
    QWidget* createInterfacePage();
    QWidget* createHotkeyPage();
    QWidget* createClipboardPage();
    QWidget* createHistoryPage();

    QKeySequenceEdit* m_hotkeySequence = nullptr;
    ConfigComboBox* m_targetLangCombo = nullptr;
    ConfigComboBox* m_toneCombo = nullptr;
    ConfigCheckBox* m_glossaryEnabled = nullptr;
    GlossaryTable* m_glossaryTable = nullptr;
    QJsonArray m_customTones;
    QPushButton* m_applyButton = nullptr;
};
