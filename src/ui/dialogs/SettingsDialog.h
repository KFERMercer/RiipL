#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QTabWidget>
#include <QVector>

#include <functional>

#include "core/config/ApiPreset.h"

class QComboBox;
class QFormLayout;
class QLabel;
class QLayout;
class QListWidget;
class QPushButton;
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

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void updateDirtyState();
    void applyChanges();
    void reloadPresets();
    void applySelectedPreset(int index);
    void newPreset();
    void overwritePreset();
    void savePreset();
    void managePresets();

private:
    QWidget* createApiPage();
    QWidget* createTranslationPage();
    QWidget* createDocumentPage();
    QWidget* createInterfacePage();
    QWidget* createHistoryPage();
    QWidget* createPromptsPage();

    // Re-applies every registered string. Rebuilding the pages instead would
    // discard pending edits.
    void retranslateUi();
    void bindText(std::function<void()> apply);

    // The layout variant serves a row whose field holds several widgets.
    QLabel* createRowLabel(QWidget* parent, const char* source);
    void addLabeledRow(QFormLayout* form, const char* source, QWidget* field);
    void addLabeledRow(QFormLayout* form, const char* source, QLayout* row);
    // \p field stacked over \p hint, for a row whose value needs a line of its own.
    static QWidget* fieldWithHint(QWidget* field, QLabel* hint);

    bool isDirty() const;
    bool canSavePreset() const;
    bool hasPendingApiEdits() const;
    bool confirmDiscard(const QString& title, const QString& text,
                        const QString& informativeText = QString());
    QJsonObject editedApiValues() const;
    void applyPresetValues(const ApiPreset& preset);

    QComboBox* m_presetCombo = nullptr;
    QPushButton* m_overwriteButton = nullptr;
    // Preset the API fields belong to, or -1 for custom settings, held across edits.
    int m_selectedPreset = -1;
    QJsonArray m_customTones;
    QVector<ApiPreset> m_apiPresets;
    QPushButton* m_applyButton = nullptr;
    HistoryManager* m_history = nullptr;
    QTabWidget* m_tabs = nullptr;
    QListWidget* m_templateList = nullptr;
    QVector<std::function<void()>> m_boundText;
};
