#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTabWidget>
#include <QVector>

#include <functional>
#include <optional>

#include "core/config/Preset.h"
#include "core/network/ApiClient.h"
#include "ui/widgets/PresetBar.h"

class ConfigEditableComboBox;
class QFormLayout;
class QLabel;
class QLayout;
class QListView;
class QPushButton;
class QStandardItemModel;
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
    void reloadModels();

private:
    QWidget* createApiPage();
    QWidget* createTranslationPage();
    QWidget* createDocumentPage();
    QWidget* createInterfacePage();
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
    // Values the editors of \p fields hold right now, pending edits included.
    QJsonObject editedValues(const QStringList& fields) const;
    // Loads \p preset into the editors of \p fields; a field the preset does not
    // carry falls back to its default.
    void applyPresetValues(const Preset& preset, const QStringList& fields);
    // Wires \p bar to the editors of \p fields and to the presets it stores;
    // \p manage runs the bar's Manage... action.
    void setupPresetBar(PresetBar* bar, const QStringList& fields,
                        std::function<std::optional<PresetBar::Managed>(const QVector<Preset>&, int)> manage);

    PresetBar* m_apiPresetBar = nullptr;
    PresetBar* m_contextPresetBar = nullptr;
    ConfigEditableComboBox* m_modelCombo = nullptr;
    // Its own client, so listing models never disturbs a running translation.
    ApiClient m_apiClient;
    QJsonArray m_customTones;
    QPushButton* m_applyButton = nullptr;
    HistoryManager* m_history = nullptr;
    QTabWidget* m_tabs = nullptr;
    QListView* m_templateList = nullptr;
    QStandardItemModel* m_templateModel = nullptr;
    QVector<std::function<void()>> m_boundText;
};
