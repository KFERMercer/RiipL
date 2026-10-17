#include "SettingsDialog.h"

#include "ApiPresetDialog.h"
#include "ContextPresetDialog.h"
#include "GlossaryDialog.h"
#include "HistoryDialog.h"
#include "LanguageDialog.h"
#include "PromptPreviewDialog.h"
#include "ToneDialog.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/document/DocumentCache.h"
#include "core/document/DocumentSegmenter.h"
#include "core/history/HistoryManager.h"
#include "core/network/ApiClient.h"
#include "core/translation/Language.h"
#include "core/translation/PromptBuilder.h"
#include "core/translation/Tone.h"
#include "core/translation/TranslationEngine.h"
#include "ui/widgets/AppFonts.h"
#include "ui/widgets/ConfigEditors.h"
#include "ui/widgets/FlowLayout.h"
#include "ui/widgets/MessageBoxes.h"
#include "ui/widgets/ThemeColors.h"
#include "ui/widgets/WindowState.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QClipboard>
#include <QCursor>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QItemSelectionModel>
#include <QListView>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <optional>
#include <utility>

namespace {

struct TemplateInfo
{
    QString configKey;
    const char* label;
};

const QVector<TemplateInfo>& templateInfos()
{
    // In the order the fragments reach the model: the system prompt leads the
    // request, the reference block follows it, and the candidate wording prompts
    // are separate requests that close the list, the short one last.
    static const QVector<TemplateInfo> list = {
        {Keys::promptSystem, QT_TRANSLATE_NOOP("SettingsDialog", "System prompt")},
        {Keys::promptReference, QT_TRANSLATE_NOOP("SettingsDialog", "Reference header")},
        {Keys::promptSourceLang, QT_TRANSLATE_NOOP("SettingsDialog", "Source language")},
        {Keys::promptTone, QT_TRANSLATE_NOOP("SettingsDialog", "Tone")},
        {Keys::promptStyle, QT_TRANSLATE_NOOP("SettingsDialog", "Style")},
        {Keys::promptBackground, QT_TRANSLATE_NOOP("SettingsDialog", "Background")},
        {Keys::promptGlossary, QT_TRANSLATE_NOOP("SettingsDialog", "Glossary")},
        {Keys::promptDefault, QT_TRANSLATE_NOOP("SettingsDialog", "Default instruction")},
        {Keys::promptDocument, QT_TRANSLATE_NOOP("SettingsDialog", "Document/YOLO")},
        {Keys::promptCandidate, QT_TRANSLATE_NOOP("SettingsDialog", "Candidate wording")},
        {Keys::promptCandidateShort,
         QT_TRANSLATE_NOOP("SettingsDialog", "Candidate wording (short text)")}
    };
    return list;
}

}

SettingsDialog::SettingsDialog(HistoryManager* history, QWidget* parent)
    : QDialog(parent)
    , m_history(history)
{
    m_customTones = ConfigManager::instance()->value(Keys::translationCustomTones).toArray();
    m_customLanguages = ConfigManager::instance()->value(Keys::translationCustomLanguages).toArray();

    auto* layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(createApiPage(), QString());
    m_tabs->addTab(createTranslationPage(), QString());
    m_tabs->addTab(createDocumentPage(), QString());
    m_tabs->addTab(createInterfacePage(), QString());
    m_tabs->addTab(createPromptsPage(), QString());
    layout->addWidget(m_tabs, 1);

    bindText([this]() {
        setWindowTitle(tr("Settings"));
        m_tabs->setTabText(0, tr("API"));
        m_tabs->setTabText(1, tr("Translation"));
        m_tabs->setTabText(2, tr("Document"));
        m_tabs->setTabText(3, tr("Interface"));
        m_tabs->setTabText(4, tr("Prompt templates"));
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply
                                         | QDialogButtonBox::Cancel, this);
    m_applyButton = buttons->button(QDialogButtonBox::Apply);
    m_applyButton->setEnabled(false);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        applyChanges();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::clicked, this,
            [this, buttons](QAbstractButton* button) {
                if (buttons->buttonRole(button) == QDialogButtonBox::ApplyRole)
                    applyChanges();
            });

    for (ConfigEditor* editor : findChildren<ConfigEditor*>()) {
        connect(editor, &ConfigEditor::edited, this, &SettingsDialog::updateDirtyState);
    }
    updateDirtyState();
    retranslateUi();

    WindowState::track(this, WindowState::Id::settings);
}

// Pages are built once and never rebuilt, so each label registers its text for
// retranslation instead of passing tr() to QFormLayout::addRow.
QLabel* SettingsDialog::createRowLabel(QWidget* parent, const char* source)
{
    auto* label = new QLabel(parent);
    bindText([label, source]() {
        label->setText(QCoreApplication::translate("SettingsDialog", source));
    });
    return label;
}

void SettingsDialog::addLabeledRow(QFormLayout* form, const char* source, QWidget* field)
{
    form->addRow(createRowLabel(field->parentWidget(), source), field);
}

void SettingsDialog::addLabeledRow(QFormLayout* form, const char* source, QLayout* row)
{
    form->addRow(createRowLabel(form->parentWidget(), source), row);
}

QWidget* SettingsDialog::fieldWithHint(QWidget* field, QLabel* hint)
{
    auto* column = new QWidget(field->parentWidget());
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(field);
    layout->addWidget(hint);
    return column;
}

void SettingsDialog::bindText(std::function<void()> apply)
{
    m_boundText.append(std::move(apply));
}

void SettingsDialog::retranslateUi()
{
    for (const std::function<void()>& apply : std::as_const(m_boundText))
        apply();
    for (ConfigEditor* editor : findChildren<ConfigEditor*>())
        editor->retranslateUi();
    for (PresetBar* bar : findChildren<PresetBar*>())
        bar->retranslateUi();
    updateDirtyState();
}

void SettingsDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
}

void SettingsDialog::reject()
{
    if (isDirty() && !MessageBoxes::confirmDiscard(this))
        return;
    QDialog::reject();
}

void SettingsDialog::updateDirtyState()
{
    m_applyButton->setEnabled(isDirty());
    if (!m_apiPresetBar || !m_contextPresetBar)
        return;
    m_apiPresetBar->refreshActions();
    m_contextPresetBar->refreshActions();
}

bool SettingsDialog::isDirty() const
{
    ConfigManager* config = ConfigManager::instance();
    if (QJsonValue(m_customTones) != config->value(Keys::translationCustomTones))
        return true;
    if (QJsonValue(m_customLanguages) != config->value(Keys::translationCustomLanguages))
        return true;
    if ((m_apiPresetBar && m_apiPresetBar->isDirty())
        || (m_contextPresetBar && m_contextPresetBar->isDirty())) {
        return true;
    }
    for (const ConfigEditor* editor : findChildren<ConfigEditor*>()) {
        if (editor->isModified())
            return true;
    }
    return false;
}

void SettingsDialog::applyChanges()
{
    ConfigManager* config = ConfigManager::instance();

    if (QJsonValue(m_customTones) != config->value(Keys::translationCustomTones))
        config->setValue(Keys::translationCustomTones, m_customTones);

    if (QJsonValue(m_customLanguages) != config->value(Keys::translationCustomLanguages))
        config->setValue(Keys::translationCustomLanguages, m_customLanguages);

    m_apiPresetBar->applyPresets();
    m_contextPresetBar->applyPresets();

    for (ConfigEditor* editor : findChildren<ConfigEditor*>()) {
        const QJsonValue editorValue = editor->value();
        if (editorValue != config->value(editor->key()))
            config->setValue(editor->key(), editorValue);
        editor->refreshBaseline();
    }
    // The committed fields stand on their own, so neither selector keeps
    // claiming a preset they no longer match.
    m_apiPresetBar->syncSelection();
    m_contextPresetBar->syncSelection();
    updateDirtyState();
}

QWidget* SettingsDialog::createApiPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    // Presets replace every field below at once, so they are grouped apart from the
    // editors they replace.
    auto* presetGroup = new QGroupBox(page);
    auto* presetLayout = new QVBoxLayout(presetGroup);
    m_apiPresetBar = new PresetBar(Keys::apiPresets, Keys::apiPresetFields(), presetGroup);
    presetLayout->addWidget(m_apiPresetBar);
    form->addRow(presetGroup);

    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Base URL"),
                  new ConfigLineEdit(Keys::apiBaseUrl, false, page));

    auto* apiKeyEdit = new ConfigLineEdit(Keys::apiKey, true, page);
    auto* copyKeyButton = new QPushButton(page);
    auto* apiKeyHint = new QLabel(page);
    auto applyApiKeyHint = [apiKeyHint]() {
        apiKeyHint->setText(tr("Stored in the configuration file as plain text"));
        ThemeColors::setTextColor(apiKeyHint, ThemeColors::neutralText(apiKeyHint));
    };
    auto* apiKeyField = new QWidget(page);
    auto* apiKeyFieldLayout = new QHBoxLayout(apiKeyField);
    apiKeyFieldLayout->setContentsMargins(0, 0, 0, 0);
    apiKeyFieldLayout->addWidget(apiKeyEdit, 1);
    apiKeyFieldLayout->addWidget(copyKeyButton);
    connect(copyKeyButton, &QPushButton::clicked, apiKeyField, [apiKeyEdit, copyKeyButton]() {
        QApplication::clipboard()->setText(apiKeyEdit->value().toString());
        QToolTip::showText(QCursor::pos(), tr("Copied"), copyKeyButton);
    });
    const auto syncCopyKeyButton = [apiKeyEdit, copyKeyButton]() {
        copyKeyButton->setEnabled(!apiKeyEdit->value().toString().isEmpty());
    };
    connect(apiKeyEdit->edit(), &QLineEdit::textChanged, apiKeyField,
            [syncCopyKeyButton](const QString&) { syncCopyKeyButton(); });
    syncCopyKeyButton();
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "API key"),
                  fieldWithHint(apiKeyField, apiKeyHint));
    applyApiKeyHint();
    bindText(applyApiKeyHint);

    m_modelCombo = new ConfigEditableComboBox(Keys::apiModel, page);
    connect(m_modelCombo, &ConfigEditableComboBox::popupAboutToShow, this,
            &SettingsDialog::reloadModels);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Model"), m_modelCombo);

    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Server connection timeout (ms)"),
                  new ConfigSpinBox(Keys::apiTimeoutMs, ApiTimeout::minimumMs, 300000, 1000, page));
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Max tokens"),
                  new ConfigSpinBox(Keys::apiMaxTokens, 1, 1000000, 256, page));

    auto* temperatureSpin = new ConfigDoubleSpinBox(Keys::apiTemperature,
                                                    ApiTemperature::providerDefaultSentinel, 2.0, 0.1, 2, page);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Temperature"), temperatureSpin);

    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Max concurrency"),
                  new ConfigSpinBox(Keys::apiMaxConcurrency, 1, 128, 1, page));

    auto* streamCheck = new ConfigCheckBox(Keys::apiStream, page);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Stream responses"), streamCheck);

    auto* headersEdit = new ConfigTextEdit(Keys::apiCustomHeaders, page);
    ConfigTextEdit::applyFixedWidthFont(headersEdit->edit());
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Custom headers"), headersEdit);

    auto* extraEdit = new ConfigTextEdit(Keys::apiExtraBody, page);
    ConfigTextEdit::applyFixedWidthFont(extraEdit->edit());
    auto* validation = new QLabel(page);
    auto updateValidation = [extraEdit, validation]() {
        const QString text = extraEdit->edit()->toPlainText().trimmed();
        if (text.isEmpty()) {
            validation->setText(tr("Empty: no extra parameters"));
            ThemeColors::setTextColor(validation, ThemeColors::neutralText(validation));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
        if (doc.isObject()) {
            validation->setText(QStringLiteral("\u2713 ") + tr("Valid JSON object"));
            ThemeColors::setTextColor(validation, ThemeColors::successText(validation));
        } else {
            validation->setText(QStringLiteral("\u2717 ") + tr("Invalid JSON: an object with key-value pairs is expected"));
            ThemeColors::setTextColor(validation, ThemeColors::errorText(validation));
        }
    };
    connect(extraEdit->edit(), &QPlainTextEdit::textChanged, page, updateValidation);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Extra body (JSON)"),
                  fieldWithHint(extraEdit, validation));
    updateValidation();
    bindText(updateValidation);

    bindText([presetGroup, temperatureSpin, headersEdit, copyKeyButton, this]() {
        presetGroup->setTitle(tr("API preset"));
        copyKeyButton->setText(tr("Copy"));
        copyKeyButton->setToolTip(tr("Copy the API key to the clipboard"));
        copyKeyButton->setAccessibleName(tr("Copy API key"));
        temperatureSpin->edit()->setSpecialValueText(tr("API default"));
        temperatureSpin->edit()->updateGeometry();
        headersEdit->edit()->setPlaceholderText(tr("One per line: Header-Name: value"));
    });
    // Wired after the editors exist, so the selector starts on the preset the
    // loaded API fields match.
    setupPresetBar(m_apiPresetBar, Keys::apiPresetFields(),
                   [this](const QVector<Preset>& presets, int selected)
                       -> std::optional<PresetBar::Managed> {
                       ApiPresetDialog dialog(presets, selected, this);
                       if (dialog.exec() != QDialog::Accepted)
                           return std::nullopt;
                       PresetBar::Managed result;
                       result.presets = dialog.presets();
                       const int loaded = dialog.loadedIndex();
                       if (loaded >= 0 && loaded < result.presets.size())
                           result.applied = result.presets.at(loaded);
                       return result;
                   });
    return page;
}

// Lists the models the API fields describe, including edits that are not applied
// yet.
void SettingsDialog::reloadModels()
{
    m_modelCombo->setMessage(tr("Loading the model list..."));
    m_apiClient.listModels(ApiClient::Connection::fromValues(editedValues(Keys::apiPresetFields())),
                           [this](const QList<ApiClient::Model>& models) {
                               QList<std::pair<QString, QString>> items;
                               items.reserve(models.size());
                               for (const ApiClient::Model& model : models)
                                   items.append({model.id, model.name});
                               if (items.isEmpty())
                                   m_modelCombo->setMessage(tr("The endpoint reported no models"));
                               else
                                   m_modelCombo->setItems(items);
                           },
                           [this](const ApiClient::Error& failure) {
                               m_modelCombo->setMessage(
                                   tr("Cannot load the model list: %1").arg(failure.text()));
                           });
}

// Fields as currently shown by the editors, so a preset records pending edits
// rather than what has already been applied.
QJsonObject SettingsDialog::editedValues(const QStringList& fields) const
{
    QJsonObject values;
    for (ConfigEditor* editor : findChildren<ConfigEditor*>()) {
        if (fields.contains(editor->key()))
            values.insert(editor->key(), editor->value());
    }
    return values;
}

void SettingsDialog::applyPresetValues(const Preset& preset, const QStringList& fields)
{
    for (ConfigEditor* editor : findChildren<ConfigEditor*>()) {
        if (!fields.contains(editor->key()))
            continue;
        const QJsonValue stored = preset.values.value(editor->key());
        editor->setUserValue(stored.isUndefined() ? Defaults::value(editor->key()) : stored);
    }
}

void SettingsDialog::setupPresetBar(
    PresetBar* bar, const QStringList& fields,
    std::function<std::optional<PresetBar::Managed>(const QVector<Preset>&, int)> manage)
{
    bar->setValuesProvider([this, fields]() { return editedValues(fields); });
    bar->setApplier([this, fields](const Preset& preset) { applyPresetValues(preset, fields); });
    bar->setManageHandler(std::move(manage));
    connect(bar, &PresetBar::changed, this, &SettingsDialog::updateDirtyState);
    bar->loadPresets();
}

QWidget* SettingsDialog::createTranslationPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    auto* sourceCombo = new ConfigComboBox(Keys::translationSourceLang, page);
    auto* targetCombo = new ConfigComboBox(Keys::translationTargetLang, page);
    auto rebuildLanguageItems = [this, sourceCombo, targetCombo]() {
        const QVector<CustomLanguage> custom = Languages::customFromJson(m_customLanguages);
        sourceCombo->setItems(languageItems(true, custom));
        targetCombo->setItems(languageItems(false, custom));
    };
    rebuildLanguageItems();
    bindText(rebuildLanguageItems);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Source language"), sourceCombo);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Target language"), targetCombo);

    auto* manageLanguages = new QPushButton(page);
    bindText([manageLanguages]() { manageLanguages->setText(tr("Custom languages")); });
    connect(manageLanguages, &QPushButton::clicked, page,
            [this, rebuildLanguageItems, page]() {
                LanguageDialog dialog(Languages::customFromJson(m_customLanguages), page);
                if (dialog.exec() != QDialog::Accepted)
                    return;
                m_customLanguages = Languages::customToJson(dialog.customLanguages());
                rebuildLanguageItems();
                updateDirtyState();
            });
    form->addRow(QString(), manageLanguages);

    auto* toneRow = new QHBoxLayout();
    auto* toneCombo = new ConfigComboBox(Keys::translationTone, page);
    auto rebuildToneItems = [this, toneCombo]() {
        toneCombo->setItems(toneItems(m_customTones));
    };
    rebuildToneItems();

    bindText(rebuildToneItems);

    auto* manageTones = new QPushButton(page);
    bindText([manageTones]() { manageTones->setText(tr("Manage...")); });
    connect(manageTones, &QPushButton::clicked, page, [this, rebuildToneItems, page]() {
        ToneDialog dialog(Tones::customFromJson(m_customTones), page);
        if (dialog.exec() == QDialog::Accepted) {
            m_customTones = Tones::customToJson(dialog.customTones());
            rebuildToneItems();
            updateDirtyState();
        }
    });
    toneRow->addWidget(toneCombo, 1);
    toneRow->addWidget(manageTones);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Tone"), static_cast<QLayout*>(toneRow));

    auto* glossaryRow = new QHBoxLayout();
    auto* glossaryEnabled = new ConfigCheckBox(Keys::glossaryEnabled, page);
    auto* manageGlossary = new QPushButton(page);
    bindText([manageGlossary]() { manageGlossary->setText(tr("Manage...")); });
    connect(manageGlossary, &QPushButton::clicked, page, [page]() {
        GlossaryDialog dialog(page);
        dialog.exec();
    });
    glossaryRow->addWidget(glossaryEnabled, 1);
    glossaryRow->addWidget(manageGlossary);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Glossary"), static_cast<QLayout*>(glossaryRow));

    auto* autoTranslateCheck = new ConfigCheckBox(Keys::uiAutoTranslate, page);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Auto translate after typing"), autoTranslateCheck);

    auto* historyRow = new QHBoxLayout();
    historyRow->addWidget(new ConfigCheckBox(Keys::historyEnabled, page), 1);
    auto* manageHistoryButton = new QPushButton(page);
    bindText([manageHistoryButton]() { manageHistoryButton->setText(tr("Manage...")); });
    connect(manageHistoryButton, &QPushButton::clicked, this, [this, page]() {
        HistoryDialog dialog(m_history, page);
        connect(&dialog, &HistoryDialog::reuseRequested, this,
                &SettingsDialog::historyReuseRequested);
        dialog.exec();
    });
    historyRow->addWidget(manageHistoryButton);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Save translation history"),
                  static_cast<QLayout*>(historyRow));

    auto* historyLimitRow = new QHBoxLayout();
    historyLimitRow->addWidget(new ConfigSpinBox(Keys::historyMaxRecords, 10, 100000, 10, page), 1);
    auto* clearHistoryButton = new QPushButton(page);
    connect(clearHistoryButton, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QCoreApplication::applicationName(),
                                  tr("Delete all history records?"))
            == QMessageBox::Yes)
            m_history->clear();
    });
    bindText([clearHistoryButton]() { clearHistoryButton->setText(tr("Clear translation history")); });
    historyLimitRow->addWidget(clearHistoryButton);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Max records"),
                  static_cast<QLayout*>(historyLimitRow));

    // The preset bar rewrites the style and the background together, so the three
    // share one frame.
    auto* contextGroup = new QGroupBox(page);
    auto* contextLayout = new QVBoxLayout(contextGroup);
    m_contextPresetBar = new PresetBar(Keys::contextPresets, Keys::contextPresetFields(), contextGroup);
    contextLayout->addWidget(m_contextPresetBar);

    auto* contextForm = new QFormLayout();
    addLabeledRow(contextForm, QT_TRANSLATE_NOOP("SettingsDialog", "Style"),
                  new ConfigTextEdit(Keys::translationStyle, contextGroup));
    addLabeledRow(contextForm, QT_TRANSLATE_NOOP("SettingsDialog", "Background"),
                  new ConfigTextEdit(Keys::translationBackground, contextGroup));
    contextLayout->addLayout(contextForm);
    form->addRow(contextGroup);

    bindText([contextGroup]() { contextGroup->setTitle(tr("Context preset")); });
    setupPresetBar(m_contextPresetBar, Keys::contextPresetFields(),
                   [this](const QVector<Preset>& presets, int selected)
                       -> std::optional<PresetBar::Managed> {
                       ContextPresetDialog dialog(presets, editedValues(Keys::contextPresetFields()),
                                                  selected, this);
                       // Applying inside the window updates this page's pending
                       // state, which the settings page's own Apply still commits.
                       dialog.setApplyHandler([this](const QVector<Preset>& edited,
                                                     const QJsonObject& values) {
                           // The editors move first, so the bar ends up claiming
                           // the preset the applied context belongs to.
                           applyPresetValues(Preset{QString(), values}, Keys::contextPresetFields());
                           m_contextPresetBar->setPresets(edited);
                           updateDirtyState();
                       });
                       if (dialog.exec() != QDialog::Accepted)
                           return std::nullopt;
                       PresetBar::Managed result;
                       result.presets = dialog.presets();
                       result.applied = Preset{QString(), dialog.values()};
                       return result;
                   });
    return page;
}

QWidget* SettingsDialog::createDocumentPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    auto* wordsSpin = new ConfigSpinBox(Keys::documentWindowWords, 1, 999999, 10, page);
    auto* linesSpin = new ConfigSpinBox(Keys::documentWindowLines,
                                        DocumentWindowLines::unlimitedSentinel, 99999, 1, page);
    bindText([linesSpin]() {
        linesSpin->edit()->setSpecialValueText(tr("Unlimited"));
        linesSpin->edit()->updateGeometry();
    });
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Window words"), wordsSpin);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Window lines"), linesSpin);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Retries"),
                  new ConfigSpinBox(Keys::documentRetryCount, 0, 999, 1, page));
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Translate concurrently"),
                  new ConfigCheckBox(Keys::documentConcurrent, page));
    auto* cacheRow = new QHBoxLayout();
    cacheRow->addWidget(new ConfigCheckBox(Keys::documentCacheEnabled, page), 1);
    auto* clearButton = new QPushButton(page);
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QCoreApplication::applicationName(),
                                  tr("Delete all document translation caches?"))
            == QMessageBox::Yes) {
            DocumentCache::clearAll();
        }
    });
    bindText([clearButton]() { clearButton->setText(tr("Clear document translation cache")); });
    cacheRow->addWidget(clearButton);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Cache translations"),
                  static_cast<QLayout*>(cacheRow));
    return page;
}

QWidget* SettingsDialog::createInterfacePage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    auto* languageCombo = new ConfigComboBox(Keys::uiLanguage, page);
    bindText([languageCombo]() { languageCombo->setItems(uiLanguageItems()); });
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Interface language"), languageCombo);

    auto* monitorCheck = new ConfigCheckBox(Keys::clipboardMonitor, page);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Monitor clipboard"), monitorCheck);

    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Enable notifications"),
                  new ConfigCheckBox(Keys::uiNotifications, page));

    auto* trayCheck = new ConfigCheckBox(Keys::uiMinimizeToTray, page);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Minimize to tray"), trayCheck);

    auto* onTopCheck = new ConfigCheckBox(Keys::uiAlwaysOnTop, page);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Always on top"), onTopCheck);

    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Font size"), new ConfigSpinBox(Keys::uiFontSize, 8, 24, 1, page));
    return page;
}

QWidget* SettingsDialog::createPromptsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* contentRow = new QHBoxLayout();

    m_templateList = new QListView(page);
    m_templateList->setAlternatingRowColors(true);
    m_templateList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_templateList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_templateList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_templateList->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_templateList->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);
    contentRow->addWidget(m_templateList);

    m_templateModel = new QStandardItemModel(this);
    for (int i = 0; i < templateInfos().size(); ++i) {
        auto* item = new QStandardItem;
        item->setEditable(false);
        m_templateModel->appendRow(item);
    }
    m_templateList->setModel(m_templateModel);
    bindText([this]() {
        const QVector<TemplateInfo>& infos = templateInfos();
        for (int i = 0; i < infos.size() && i < m_templateModel->rowCount(); ++i)
            m_templateModel->item(i)->setText(tr(infos.at(i).label));
    });

    auto* stack = new QStackedWidget(page);
    for (const TemplateInfo& info : templateInfos()) {
        auto* pageWidget = new QWidget(stack);
        auto* pageLayout = new QVBoxLayout(pageWidget);
        auto* editor = new ConfigTextEdit(info.configKey, pageWidget);
        ConfigTextEdit::applyFixedWidthFont(editor->edit());
        pageLayout->addWidget(editor);

        auto* placeholderGroup = new QGroupBox(pageWidget);
        bindText([placeholderGroup]() {
            placeholderGroup->setTitle(tr("Available placeholders (click to copy)"));
        });
        auto* hintLayout = new FlowLayout(placeholderGroup);
        // Each chip explains the token it inserts instead of repeating the title.
        const auto placeholderHint = [](const QString& placeholder) {
            if (placeholder == QLatin1String("source_lang")) return tr("Language of the source text");
            if (placeholder == QLatin1String("target_lang")) return tr("Language to translate into");
            if (placeholder == QLatin1String("tone")) return tr("Tone applied to the translation");
            if (placeholder == QLatin1String("style")) return tr("Style applied to the translation");
            if (placeholder == QLatin1String("background")) return tr("Background information");
            if (placeholder == QLatin1String("glossary")) return tr("Glossary entries, rendered as JSON");
            if (placeholder == QLatin1String("source_text")) return tr("Text to be translated");
            if (placeholder == QLatin1String("translated_text")) return tr("Full translated text");
            if (placeholder == QLatin1String("selected_fragment")) return tr("Translation fragment around the selected word (candidate wording only)");
            if (placeholder == QLatin1String("selected_word")) return tr("Word selected in the translation pane (candidate wording only)");
            if (placeholder == QLatin1String("mark_left")) return tr("Marker placed before the selected word (candidate wording only)");
            if (placeholder == QLatin1String("mark_right")) return tr("Marker placed after the selected word (candidate wording only)");
            if (placeholder == QLatin1String("window")) return tr("Translation window, rendered as JSON (document translation only)");
            if (placeholder == QLatin1String("window_lines")) return tr("Number of lines in the translation window (document translation only)");
            if (placeholder == QLatin1String("prev_segment")) return tr("Previous segment to translate (document translation only)");
            if (placeholder == QLatin1String("next_segment")) return tr("Next segment to translate (document translation only)");
            return QString();
        };
        for (const QString& placeholder : PromptBuilder::knownPlaceholders()) {
            const QString token = QLatin1Char('{') + placeholder + QLatin1Char('}');
            auto* chip = new QToolButton(placeholderGroup);
            chip->setText(token);
            chip->setFont(AppFonts::fixedWidth());
            bindText([chip, placeholder, placeholderHint]() {
                chip->setToolTip(placeholderHint(placeholder));
            });
            chip->setCursor(Qt::PointingHandCursor);
            chip->setAutoRaise(true);
            connect(chip, &QToolButton::clicked, chip, [chip, token]() {
                QApplication::clipboard()->setText(token);
                QToolTip::showText(QCursor::pos(), tr("Copied"), chip);
            });
            hintLayout->addWidget(chip);
        }
        pageLayout->addWidget(placeholderGroup);
        stack->addWidget(pageWidget);
    }
    connect(m_templateList->selectionModel(), &QItemSelectionModel::currentRowChanged, stack,
            [stack](const QModelIndex& current) { stack->setCurrentIndex(current.row()); });
    m_templateList->setCurrentIndex(m_templateModel->index(0, 0));
    contentRow->addWidget(stack, 1);
    layout->addLayout(contentRow, 1);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    auto* testButton = new QPushButton(page);
    bindText([testButton]() { testButton->setText(tr("Prompt preview")); });
    connect(testButton, &QPushButton::clicked, page, [page]() {
        PromptPreviewDialog dialog(page);
        dialog.exec();
    });
    buttonRow->addWidget(testButton);
    layout->addLayout(buttonRow);
    return page;
}
