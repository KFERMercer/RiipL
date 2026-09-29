#include "SettingsDialog.h"

#include "ApiPresetDialog.h"
#include "GlossaryDialog.h"
#include "PromptPreviewDialog.h"
#include "ToneDialog.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/history/HistoryManager.h"
#include "core/translation/PromptBuilder.h"
#include "ui/widgets/AppFonts.h"
#include "ui/widgets/ConfigEditors.h"
#include "ui/widgets/FlowLayout.h"
#include "ui/widgets/ThemeColors.h"
#include "ui/widgets/WindowState.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCursor>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

namespace {

struct TemplateInfo
{
    QString key;
    const char* label;
};

const QVector<TemplateInfo>& templateInfos()
{
    // Listed in the order the fragments reach the model: the system prompt
    // leads the request, the reference block follows it, and the candidate
    // wording prompts are separate requests that close the list, the one for a
    // short translation last.
    static const QVector<TemplateInfo> list = {
        {Prompts::systemTemplate, QT_TRANSLATE_NOOP("SettingsDialog", "System prompt")},
        {Prompts::referenceTemplate, QT_TRANSLATE_NOOP("SettingsDialog", "Reference header")},
        {Prompts::toneTemplate, QT_TRANSLATE_NOOP("SettingsDialog", "Tone")},
        {Prompts::styleTemplate, QT_TRANSLATE_NOOP("SettingsDialog", "Style")},
        {Prompts::backgroundTemplate, QT_TRANSLATE_NOOP("SettingsDialog", "Background")},
        {Prompts::glossaryTemplate, QT_TRANSLATE_NOOP("SettingsDialog", "Glossary")},
        {Prompts::defaultTemplate, QT_TRANSLATE_NOOP("SettingsDialog", "Default instruction")},
        {Prompts::candidateTemplate, QT_TRANSLATE_NOOP("SettingsDialog", "Candidate wording")},
        {Prompts::candidateShortTemplate,
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
    m_apiPresets = ApiPresets::fromJson(ConfigManager::instance()->value(Keys::apiPresets).toArray());

    auto* layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(createApiPage(), QString());
    m_tabs->addTab(createTranslationPage(), QString());
    m_tabs->addTab(createInterfacePage(), QString());
    m_tabs->addTab(createClipboardPage(), QString());
    m_tabs->addTab(createHistoryPage(), QString());
    m_tabs->addTab(createPromptsPage(), QString());
    layout->addWidget(m_tabs, 1);

    bindText([this]() {
        setWindowTitle(tr("Settings"));
        m_tabs->setTabText(0, tr("API"));
        m_tabs->setTabText(1, tr("Translation"));
        m_tabs->setTabText(2, tr("Interface"));
        m_tabs->setTabText(3, tr("Clipboard"));
        m_tabs->setTabText(4, tr("History"));
        m_tabs->setTabText(5, tr("Prompt templates"));
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

void SettingsDialog::bindText(const std::function<void()>& apply)
{
    m_boundText.append(apply);
}

void SettingsDialog::retranslateUi()
{
    for (const std::function<void()>& apply : std::as_const(m_boundText))
        apply();
    for (ConfigEditor* editor : findChildren<ConfigEditor*>())
        editor->retranslateUi();
    updateDirtyState();
}

void SettingsDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
}

// Cancel is the default, so a stray Return never drops pending edits.
bool SettingsDialog::confirmDiscard(const QString& title, const QString& text,
                                    const QString& informativeText)
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(title);
    box.setText(text);
    if (!informativeText.isEmpty())
        box.setInformativeText(informativeText);
    box.setStandardButtons(QMessageBox::Discard | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    return box.exec() == QMessageBox::Discard;
}

void SettingsDialog::reject()
{
    if (isDirty() && !confirmDiscard(tr("Unsaved changes"), tr("Your changes have not been applied yet.")))
        return;
    QDialog::reject();
}

void SettingsDialog::updateDirtyState()
{
    m_applyButton->setEnabled(isDirty());
    if (!m_overwriteButton)
        return;
    m_overwriteButton->setEnabled(canSavePreset());
    m_overwriteButton->setToolTip(m_selectedPreset >= 0 && m_selectedPreset < m_apiPresets.size()
            ? tr("Overwrite \"%1\" with the current API settings").arg(m_apiPresets.at(m_selectedPreset).name)
            : tr("Save the current API settings as a new preset"));
}

bool SettingsDialog::isDirty() const
{
    ConfigManager* config = ConfigManager::instance();
    if (QJsonValue(m_customTones) != config->value(Keys::translationCustomTones))
        return true;
    if (QJsonValue(ApiPresets::toJson(m_apiPresets)) != config->value(Keys::apiPresets))
        return true;
    for (const ConfigEditor* editor : findChildren<ConfigEditor*>()) {
        if (editor->isModified())
            return true;
    }
    return false;
}

// Follows the rule Apply uses: the button is offered only while it would change
// something.
bool SettingsDialog::canSavePreset() const
{
    const QJsonObject edited = editedApiValues();
    if (m_selectedPreset >= 0 && m_selectedPreset < m_apiPresets.size()) {
        return ApiPresets::withDefaults(edited)
            != ApiPresets::withDefaults(m_apiPresets.at(m_selectedPreset).values);
    }
    return hasPendingApiEdits();
}

// Only the fields a preset captures; preset actions rewrite nothing else.
bool SettingsDialog::hasPendingApiEdits() const
{
    for (const ConfigEditor* editor : findChildren<ConfigEditor*>()) {
        if (Keys::apiPresetFields().contains(editor->key()) && editor->isModified())
            return true;
    }
    return false;
}

void SettingsDialog::applyChanges()
{
    ConfigManager* config = ConfigManager::instance();

    if (QJsonValue(m_customTones) != config->value(Keys::translationCustomTones))
        config->setValue(Keys::translationCustomTones, m_customTones);

    if (QJsonValue(ApiPresets::toJson(m_apiPresets)) != config->value(Keys::apiPresets))
        config->setValue(Keys::apiPresets, ApiPresets::toJson(m_apiPresets));

    for (ConfigEditor* editor : findChildren<ConfigEditor*>()) {
        const QJsonValue editorValue = editor->value();
        if (editorValue != config->value(editor->key()))
            config->setValue(editor->key(), editorValue);
        editor->refreshBaseline();
    }
    // The committed fields stand on their own, so the selector stops claiming a
    // preset they no longer match.
    m_selectedPreset = ApiPresets::matchValues(m_apiPresets, editedApiValues());
    reloadPresets();
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
    auto* presetRow = new QHBoxLayout();
    m_presetCombo = new QComboBox(presetGroup);
    auto* newPresetButton = new QPushButton(presetGroup);
    m_overwriteButton = new QPushButton(presetGroup);
    auto* saveAsPresetButton = new QPushButton(presetGroup);
    auto* managePresetButton = new QPushButton(presetGroup);
    presetRow->addWidget(m_presetCombo, 1);
    presetRow->addWidget(newPresetButton);
    presetRow->addWidget(m_overwriteButton);
    presetRow->addWidget(saveAsPresetButton);
    presetRow->addWidget(managePresetButton);
    presetLayout->addLayout(presetRow);
    form->addRow(presetGroup);

    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Base URL"), new ConfigLineEdit(Keys::apiBaseUrl, false, page));
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "API key"), new ConfigLineEdit(Keys::apiKey, true, page));
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Model"), new ConfigLineEdit(Keys::apiModel, false, page));
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Server connection timeout (ms)"),
                  new ConfigSpinBox(Keys::apiTimeoutMs, 1000, 300000, 1000, page));
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Max tokens"),
                  new ConfigSpinBox(Keys::apiMaxTokens, 1, 1000000, 256, page));

    auto* temperatureSpin = new ConfigDoubleSpinBox(Keys::apiTemperature, -0.1, 2.0, 0.1, 2, page);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Temperature"), temperatureSpin);

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
    auto* extraField = new QWidget(page);
    auto* extraLayout = new QVBoxLayout(extraField);
    extraLayout->setContentsMargins(0, 0, 0, 0);
    extraLayout->setSpacing(0);
    extraLayout->addWidget(extraEdit);
    extraLayout->addWidget(validation);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Extra body (JSON)"), extraField);
    updateValidation();
    bindText(updateValidation);

    bindText([presetGroup, newPresetButton, saveAsPresetButton, managePresetButton, temperatureSpin, headersEdit, this]() {
        presetGroup->setTitle(tr("API preset"));
        m_presetCombo->setPlaceholderText(tr("Empty preset"));
        newPresetButton->setText(tr("New"));
        newPresetButton->setToolTip(tr("Create an empty preset"));
        newPresetButton->setAccessibleName(tr("New preset"));
        m_overwriteButton->setText(tr("Save preset"));
        saveAsPresetButton->setText(tr("Save as..."));
        managePresetButton->setText(tr("Manage..."));
        saveAsPresetButton->setToolTip(tr("Save the current API settings under a new name"));
        managePresetButton->setToolTip(tr("Rename, copy, reorder, delete or load API presets"));
        temperatureSpin->edit()->setSpecialValueText(tr("API default"));
        temperatureSpin->edit()->updateGeometry();
        headersEdit->edit()->setPlaceholderText(tr("One per line: Header-Name: value"));
    });
    connect(m_presetCombo, &QComboBox::activated, this, &SettingsDialog::applySelectedPreset);
    connect(newPresetButton, &QPushButton::clicked, this, &SettingsDialog::newPreset);
    connect(m_overwriteButton, &QPushButton::clicked, this, &SettingsDialog::overwritePreset);
    connect(saveAsPresetButton, &QPushButton::clicked, this, &SettingsDialog::savePreset);
    connect(managePresetButton, &QPushButton::clicked, this, &SettingsDialog::managePresets);
    // The applied configuration decides which preset the selector starts on.
    m_selectedPreset = ApiPresets::matchValues(m_apiPresets, editedApiValues());
    reloadPresets();
    return page;
}

// Rebuilds the selector and points it at the preset in effect. The selection is
// held across edits, which is what makes overwriting the selected preset
// possible.
void SettingsDialog::reloadPresets()
{
    if (!m_presetCombo)
        return;
    QSignalBlocker blocker(m_presetCombo);
    m_presetCombo->clear();
    for (const ApiPreset& preset : std::as_const(m_apiPresets))
        m_presetCombo->addItem(preset.name);
    const bool held = m_selectedPreset >= 0 && m_selectedPreset < m_apiPresets.size();
    m_presetCombo->setCurrentIndex(held ? m_selectedPreset : -1);
}

// API fields as currently shown by the editors, so a preset records pending
// edits rather than what has already been applied.
QJsonObject SettingsDialog::editedApiValues() const
{
    QJsonObject values;
    for (ConfigEditor* editor : findChildren<ConfigEditor*>()) {
        if (Keys::apiPresetFields().contains(editor->key()))
            values.insert(editor->key(), editor->value());
    }
    return values;
}

void SettingsDialog::applyPresetValues(const ApiPreset& preset)
{
    for (ConfigEditor* editor : findChildren<ConfigEditor*>()) {
        if (!Keys::apiPresetFields().contains(editor->key()))
            continue;
        const QJsonValue stored = preset.values.value(editor->key());
        editor->setUserValue(stored.isUndefined() ? Defaults::value(editor->key()) : stored);
    }
}

void SettingsDialog::applySelectedPreset(int index)
{
    if (index < 0 || index >= m_apiPresets.size())
        return;
    m_selectedPreset = index;
    applyPresetValues(m_apiPresets.at(index));
    updateDirtyState();
}

// Starts a preset from the default fields, named later by a save action.
void SettingsDialog::newPreset()
{
    if (hasPendingApiEdits()
        && !confirmDiscard(tr("New preset"),
                           tr("The API fields hold changes that have not been applied yet."),
                           tr("Discard them and start from the defaults?"))) {
        return;
    }

    applyPresetValues(ApiPreset{});
    m_selectedPreset = -1;
    reloadPresets();
    updateDirtyState();
}

// Writes the pending API fields into the selected preset. With nothing selected
// there is no preset to overwrite yet, so the settings are named instead.
void SettingsDialog::overwritePreset()
{
    const int index = m_selectedPreset;
    if (index < 0 || index >= m_apiPresets.size()) {
        savePreset();
        return;
    }
    m_apiPresets[index].values = editedApiValues();
    reloadPresets();
    updateDirtyState();
}

// Adds a preset under a name the user picks, replacing the entry when the name
// is taken.
void SettingsDialog::savePreset()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Save API preset"), tr("Preset name"),
                                              QLineEdit::Normal, m_presetCombo->currentText(),
                                              &accepted).trimmed();
    if (!accepted || name.isEmpty())
        return;

    ApiPreset preset;
    preset.name = name;
    preset.values = editedApiValues();

    const int existing = ApiPresets::indexOf(m_apiPresets, name);
    if (existing >= 0)
        m_apiPresets[existing] = preset;
    else
        m_apiPresets.append(preset);
    // The written fields match that preset, so it becomes the selected one.
    m_selectedPreset = ApiPresets::indexOf(m_apiPresets, name);
    reloadPresets();
    updateDirtyState();
}

void SettingsDialog::managePresets()
{
    // An explicit selection wins over the match, since pending edits may have
    // taken the fields off it.
    int selected = m_selectedPreset;
    if (selected < 0 || selected >= m_apiPresets.size())
        selected = ApiPresets::matchValues(m_apiPresets, editedApiValues());
    ApiPresetDialog dialog(m_apiPresets, selected, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_apiPresets = dialog.presets();
    // Accepting the manager loads the highlighted preset over the pending edits.
    const int loaded = dialog.loadedIndex();
    if (loaded >= 0 && loaded < m_apiPresets.size()) {
        applyPresetValues(m_apiPresets.at(loaded));
        m_selectedPreset = loaded;
    } else {
        m_selectedPreset = ApiPresets::matchValues(m_apiPresets, editedApiValues());
    }
    reloadPresets();
    updateDirtyState();
}

QWidget* SettingsDialog::createTranslationPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    auto* sourceCombo = new ConfigComboBox(Keys::translationSourceLang, page);
    auto* targetCombo = new ConfigComboBox(Keys::translationTargetLang, page);
    bindText([sourceCombo, targetCombo]() {
        sourceCombo->setItems(languageItems(true));
        targetCombo->setItems(languageItems(false));
    });
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Source language"), sourceCombo);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Target language"), targetCombo);

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
        ToneDialog dialog(m_customTones, page);
        if (dialog.exec() == QDialog::Accepted) {
            m_customTones = ToneDialog::toJson(dialog.customTones());
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

    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Style"), new ConfigTextEdit(Keys::translationStyle, page));
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Background"), new ConfigTextEdit(Keys::translationBackground, page));

    auto* autoTranslateCheck = new ConfigCheckBox(Keys::uiAutoTranslate, page);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Auto translate after typing"), autoTranslateCheck);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Auto translate delay (ms)"), new ConfigSpinBox(Keys::uiAutoTranslateDelay, 100, 10000, 100, page));
    return page;
}

QWidget* SettingsDialog::createInterfacePage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    auto* languageCombo = new ConfigComboBox(Keys::uiLanguage, page);
    bindText([languageCombo]() { languageCombo->setItems(uiLanguageItems()); });
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Interface language"), languageCombo);

    auto* onTopCheck = new ConfigCheckBox(Keys::uiAlwaysOnTop, page);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Keep window on top"), onTopCheck);

    auto* trayCheck = new ConfigCheckBox(Keys::uiMinimizeToTray, page);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Minimize to tray on close"), trayCheck);

    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Font size"), new ConfigSpinBox(Keys::uiFontSize, 8, 24, 1, page));
    return page;
}

QWidget* SettingsDialog::createClipboardPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    auto* monitorCheck = new ConfigCheckBox(Keys::clipboardMonitor, page);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Monitor clipboard and translate automatically"), monitorCheck);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Monitor delay (ms)"), new ConfigSpinBox(Keys::clipboardDelayMs, 100, 5000, 50, page));
    return page;
}

QWidget* SettingsDialog::createHistoryPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    auto* enabledCheck = new ConfigCheckBox(Keys::historyEnabled, page);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Save translation history"), enabledCheck);
    addLabeledRow(form, QT_TRANSLATE_NOOP("SettingsDialog", "Max records"), new ConfigSpinBox(Keys::historyMaxRecords, 10, 100000, 10, page));

    auto* clearButton = new QPushButton(page);
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, tr("RiipL"), tr("Delete all history records?"))
            == QMessageBox::Yes)
            m_history->clear();
    });
    form->addRow(QString(), clearButton);
    bindText([clearButton]() { clearButton->setText(tr("Clear history now")); });
    return page;
}

QWidget* SettingsDialog::createPromptsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* contentRow = new QHBoxLayout();

    m_templateList = new QListWidget(page);
    m_templateList->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_templateList->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);
    for (int i = 0; i < templateInfos().size(); ++i)
        m_templateList->addItem(QString());
    contentRow->addWidget(m_templateList);
    bindText([this]() {
        const QVector<TemplateInfo>& infos = templateInfos();
        for (int i = 0; i < infos.size() && i < m_templateList->count(); ++i)
            m_templateList->item(i)->setText(tr(infos.at(i).label));
    });

    auto* stack = new QStackedWidget(page);
    for (const TemplateInfo& info : templateInfos()) {
        auto* pageWidget = new QWidget(stack);
        auto* pageLayout = new QVBoxLayout(pageWidget);
        auto* langTabs = new QTabWidget(pageWidget);
        auto* zhEditor = new ConfigTextEdit(Keys::promptKey(info.key, QStringLiteral("zh")), pageWidget);
        ConfigTextEdit::applyFixedWidthFont(zhEditor->edit());
        auto* enEditor = new ConfigTextEdit(Keys::promptKey(info.key, QStringLiteral("en")), pageWidget);
        ConfigTextEdit::applyFixedWidthFont(enEditor->edit());
        langTabs->addTab(zhEditor, QString());
        langTabs->addTab(enEditor, QString());
        pageLayout->addWidget(langTabs);

        auto* placeholderGroup = new QGroupBox(pageWidget);
        bindText([langTabs, placeholderGroup]() {
            langTabs->setTabText(0, tr("Chinese template"));
            langTabs->setTabText(1, tr("English template"));
            placeholderGroup->setTitle(tr("Available placeholders (click to copy)"));
        });
        auto* hintLayout = new FlowLayout(placeholderGroup);
        // Describes what each placeholder inserts, so the chip tooltip explains
        // the token instead of repeating the group title.
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
    connect(m_templateList, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
    m_templateList->setCurrentRow(0);
    contentRow->addWidget(stack, 1);
    layout->addLayout(contentRow, 1);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    auto* testButton = new QPushButton(page);
    bindText([testButton]() { testButton->setText(tr("Preview prompt...")); });
    connect(testButton, &QPushButton::clicked, page, [page]() {
        PromptPreviewDialog dialog(page);
        dialog.exec();
    });
    buttonRow->addWidget(testButton);
    layout->addLayout(buttonRow);
    return page;
}
