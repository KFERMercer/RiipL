#include "SettingsDialog.h"

#include "ApiPresetDialog.h"
#include "GlossaryDialog.h"
#include "ToneDialog.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/history/HistoryManager.h"
#include "core/models/Glossary.h"
#include "core/translation/Language.h"
#include "core/translation/PromptBuilder.h"
#include "core/translation/Tone.h"
#include "ui/widgets/ConfigEditors.h"
#include "ui/widgets/FlowLayout.h"
#include "ui/widgets/ThemeColors.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
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
    QString labelEn;
    QString labelZh;
};

const QVector<TemplateInfo>& templateInfos()
{
    static const QVector<TemplateInfo> list = {
        {Prompts::defaultTemplate, QStringLiteral("Default"), QStringLiteral("默认指令")},
        {Prompts::systemTemplate, QStringLiteral("System prompt"), QStringLiteral("系统提示词")},
        {Prompts::referenceTemplate, QStringLiteral("Reference header"), QStringLiteral("参考信息标题")},
        {Prompts::glossaryTemplate, QStringLiteral("Glossary"), QStringLiteral("术语表")},
        {Prompts::toneTemplate, QStringLiteral("Tone"), QStringLiteral("语气")},
        {Prompts::styleTemplate, QStringLiteral("Style"), QStringLiteral("风格")},
        {Prompts::backgroundTemplate, QStringLiteral("Background"), QStringLiteral("背景信息")},
        {Prompts::candidateTemplate, QStringLiteral("Candidate wording"), QStringLiteral("候选遣词")}
    };
    return list;
}

}

class PromptPreviewDialog : public QDialog
{
    Q_OBJECT

public:
    PromptPreviewDialog(const QString& targetLang, const QString& tone,
                        bool glossaryEnabled, const QVector<GlossaryEntry>& glossary,
                        QWidget* parent)
        : QDialog(parent)
        , m_glossaryEnabled(glossaryEnabled)
        , m_glossary(glossary)
    {
        setWindowTitle(tr("Prompt preview"));
        auto* layout = new QVBoxLayout(this);

        auto* form = new QFormLayout();
        m_source = new QPlainTextEdit(this);
        m_source->setMinimumHeight(m_source->fontMetrics().lineSpacing() * 3);
        m_source->setPlainText(QStringLiteral("Hello, world! RiipL is a translation tool."));
        m_target = new QComboBox(this);
        const QString uiLanguage = ConfigManager::instance()->resolvedUiLanguage();
        for (const LangItem& lang : Languages::all()) {
            if (lang.code == QLatin1String("auto"))
                continue;
            m_target->addItem(Languages::displayName(lang.code, uiLanguage), lang.code);
        }
        m_target->setCurrentIndex(m_target->findData(targetLang));
        m_tone = new QComboBox(this);
        for (const ToneItem& item : Tones::presets())
            m_tone->addItem(Tones::presetDisplayName(item.key, uiLanguage), item.key);
        m_tone->setCurrentIndex(m_tone->findData(tone));
        m_style = new QLineEdit(this);
        m_background = new QLineEdit(this);
        form->addRow(tr("Sample text"), m_source);
        form->addRow(tr("Target language"), m_target);
        form->addRow(tr("Tone"), m_tone);
        form->addRow(tr("Style"), m_style);
        form->addRow(tr("Background"), m_background);
        layout->addLayout(form);

        m_output = new QPlainTextEdit(this);
        m_output->setReadOnly(true);
        layout->addWidget(m_output, 1);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

        connect(m_source, &QPlainTextEdit::textChanged, this, &PromptPreviewDialog::refresh);
        connect(m_target, &QComboBox::currentIndexChanged, this, &PromptPreviewDialog::refresh);
        connect(m_tone, &QComboBox::currentIndexChanged, this, &PromptPreviewDialog::refresh);
        connect(m_style, &QLineEdit::textChanged, this, &PromptPreviewDialog::refresh);
        connect(m_background, &QLineEdit::textChanged, this, &PromptPreviewDialog::refresh);
        refresh();
    }

private slots:
    void refresh()
    {
        ConfigManager* config = ConfigManager::instance();
        TranslationContext context;
        context.sourceText = m_source->toPlainText();
        context.targetLang = m_target->currentData().toString();
        context.tone = m_tone->currentData().toString();
        context.style = m_style->text().trimmed();
        context.background = m_background->text().trimmed();
        context.glossaryEnabled = m_glossaryEnabled;
        context.glossary = m_glossary;
        context.uiLanguage = config->resolvedUiLanguage();
        const PromptBuilder::Result result = PromptBuilder::build(context);
        QString text;
        if (!result.system.isEmpty())
            text += QStringLiteral("[system]\n%1\n\n").arg(result.system);
        text += result.user;
        m_output->setPlainText(text.isEmpty() ? tr("(empty prompt)") : text);
    }

private:
    bool m_glossaryEnabled = false;
    QVector<GlossaryEntry> m_glossary;
    QPlainTextEdit* m_source = nullptr;
    QComboBox* m_target = nullptr;
    QComboBox* m_tone = nullptr;
    QLineEdit* m_style = nullptr;
    QLineEdit* m_background = nullptr;
    QPlainTextEdit* m_output = nullptr;
};

SettingsDialog::SettingsDialog(HistoryManager* history, QWidget* parent)
    : QDialog(parent)
    , m_history(history)
{
    setWindowTitle(tr("Settings"));

    m_customTones = ConfigManager::instance()->value(Keys::translationCustomTones).toArray();
    m_apiPresets = ApiPresets::fromJson(ConfigManager::instance()->value(Keys::apiPresets).toArray());

    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    tabs->addTab(createApiPage(), tr("API"));
    tabs->addTab(createTranslationPage(), tr("Translation"));
    tabs->addTab(createInterfacePage(), tr("Interface"));
    tabs->addTab(createClipboardPage(), tr("Clipboard"));
    tabs->addTab(createHistoryPage(), tr("History"));
    tabs->addTab(createPromptsPage(), tr("Prompt templates"));
    layout->addWidget(tabs, 1);

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
        // Editing any captured field can take the settings off a preset, so the
        // selector is re-matched as the user types.
        if (Keys::apiPresetFields().contains(editor->key()))
            connect(editor, &ConfigEditor::edited, this, &SettingsDialog::reloadPresets);
    }

}

void SettingsDialog::reject()
{
    if (isDirty()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Unsaved changes"));
        box.setText(tr("Your changes have not been applied yet."));
        box.setStandardButtons(QMessageBox::Discard | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() != QMessageBox::Discard)
            return;
    }
    QDialog::reject();
}

void SettingsDialog::updateDirtyState()
{
    m_applyButton->setEnabled(isDirty());
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
    updateDirtyState();
}

QWidget* SettingsDialog::createApiPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    // Presets switch every field below at once, so they sit above the editors
    // they replace rather than inside the tab's form proper.
    auto* presetRow = new QWidget(page);
    auto* presetLayout = new QHBoxLayout(presetRow);
    presetLayout->setContentsMargins(0, 0, 0, 0);
    m_presetCombo = new QComboBox(presetRow);
    m_presetCombo->setPlaceholderText(tr("Custom settings"));
    auto* savePresetButton = new QPushButton(tr("Save as preset..."), presetRow);
    auto* managePresetButton = new QPushButton(tr("Manage..."), presetRow);
    savePresetButton->setToolTip(tr("Save the current API settings under a name"));
    managePresetButton->setToolTip(tr("Rename, reorder, delete or load API presets"));
    presetLayout->addWidget(m_presetCombo, 1);
    presetLayout->addWidget(savePresetButton);
    presetLayout->addWidget(managePresetButton);
    form->addRow(tr("API preset"), presetRow);

    form->addRow(tr("Base URL"), new ConfigLineEdit(Keys::apiBaseUrl, false, page));
    form->addRow(tr("API key"), new ConfigLineEdit(Keys::apiKey, true, page));
    form->addRow(tr("Model"), new ConfigLineEdit(Keys::apiModel, false, page));
    form->addRow(tr("Server connection timeout (ms)"),
                 new ConfigSpinBox(Keys::apiTimeoutMs, 1000, 300000, 1000, page));
    form->addRow(tr("Max tokens"), new ConfigSpinBox(Keys::apiMaxTokens, 1, 1000000, 256, page));

    auto* temperatureSpin = new ConfigDoubleSpinBox(Keys::apiTemperature, -0.1, 2.0, 0.1, 2, page);
    temperatureSpin->edit()->setSpecialValueText(tr("API default"));
    form->addRow(tr("Temperature"), temperatureSpin);

    auto* streamCheck = new ConfigCheckBox(Keys::apiStream, page);
    form->addRow(tr("Stream responses"), streamCheck);

    auto* headersEdit = new ConfigTextEdit(Keys::apiCustomHeaders, 4, page);
    headersEdit->edit()->setPlaceholderText(tr("One per line: Header-Name: value"));
    form->addRow(tr("Custom headers"), headersEdit);

    auto* extraEdit = new ConfigTextEdit(Keys::apiExtraBody, 4, page);
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
    form->addRow(tr("Extra body (JSON)"), extraField);
    updateValidation();

    connect(m_presetCombo, &QComboBox::activated, this, &SettingsDialog::applySelectedPreset);
    connect(savePresetButton, &QPushButton::clicked, this, &SettingsDialog::savePreset);
    connect(managePresetButton, &QPushButton::clicked, this, &SettingsDialog::managePresets);
    reloadPresets();
    return page;
}

// Rebuilds the selector from the in-memory list. The entry whose fields still
// match the pending settings is selected; when none does, the selector clears
// to its placeholder.
void SettingsDialog::reloadPresets()
{
    if (!m_presetCombo)
        return;
    QSignalBlocker blocker(m_presetCombo);
    m_presetCombo->clear();
    for (const ApiPreset& preset : std::as_const(m_apiPresets))
        m_presetCombo->addItem(preset.name);
    m_presetCombo->setCurrentIndex(ApiPresets::matchValues(m_apiPresets, editedApiValues()));
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
    applyPresetValues(m_apiPresets.at(index));
    updateDirtyState();
}

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
    reloadPresets();
    updateDirtyState();
}

void SettingsDialog::managePresets()
{
    // The settings the user is looking at decide the initial highlight, since
    // they may hold edits that have not been applied yet.
    const int selected = ApiPresets::matchValues(m_apiPresets, editedApiValues());
    ApiPresetDialog dialog(m_apiPresets, selected, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_apiPresets = dialog.presets();
    // A load request inside the dialog replaces the pending API edits, which
    // the selector is then re-matched against.
    const int loaded = dialog.loadedIndex();
    if (loaded >= 0 && loaded < m_apiPresets.size())
        applyPresetValues(m_apiPresets.at(loaded));
    reloadPresets();
    updateDirtyState();
}

QWidget* SettingsDialog::createTranslationPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    const QString uiLanguage = ConfigManager::instance()->resolvedUiLanguage();
    auto* sourceCombo = new ConfigComboBox(Keys::translationSourceLang, page);
    QList<QPair<QString, QString>> sourceItems;
    for (const LangItem& lang : Languages::all())
        sourceItems.append({Languages::displayName(lang.code, uiLanguage), lang.code});
    sourceCombo->setItems(sourceItems);
    form->addRow(tr("Source language"), sourceCombo);

    m_targetLangCombo = new ConfigComboBox(Keys::translationTargetLang, page);
    QList<QPair<QString, QString>> targetItems;
    for (const LangItem& lang : Languages::all()) {
        if (lang.code == QLatin1String("auto"))
            continue;
        targetItems.append({Languages::displayName(lang.code, uiLanguage), lang.code});
    }
    m_targetLangCombo->setItems(targetItems);
    form->addRow(tr("Target language"), m_targetLangCombo);

    auto* toneRow = new QHBoxLayout();
    m_toneCombo = new ConfigComboBox(Keys::translationTone, page);
    auto rebuildToneItems = [this, uiLanguage]() {
        QList<QPair<QString, QString>> items;
        for (const ToneItem& tone : Tones::presets())
            items.append({Tones::presetDisplayName(tone.key, uiLanguage), tone.key});
        for (const QJsonValue& value : std::as_const(m_customTones)) {
            const QJsonObject object = value.toObject();
            const QString key = object.value(QStringLiteral("key")).toString();
            items.append({object.value(QStringLiteral("name")).toString(key), key});
        }
        m_toneCombo->setItems(items);
    };
    rebuildToneItems();

    auto* manageTones = new QPushButton(tr("Manage..."), page);
    connect(manageTones, &QPushButton::clicked, page, [this, rebuildToneItems, page]() {
        ToneDialog dialog(m_customTones, ConfigManager::instance()->resolvedUiLanguage(), page);
        if (dialog.exec() == QDialog::Accepted) {
            m_customTones = ToneDialog::toJson(dialog.customTones());
            rebuildToneItems();
            updateDirtyState();
        }
    });
    toneRow->addWidget(m_toneCombo, 1);
    toneRow->addWidget(manageTones);
    form->addRow(tr("Tone"), toneRow);

    auto* glossaryRow = new QHBoxLayout();
    m_glossaryEnabled = new ConfigCheckBox(Keys::glossaryEnabled, page);
    auto* manageGlossary = new QPushButton(tr("Manage..."), page);
    connect(manageGlossary, &QPushButton::clicked, page, [page]() {
        GlossaryDialog dialog(page);
        dialog.exec();
    });
    glossaryRow->addWidget(m_glossaryEnabled, 1);
    glossaryRow->addWidget(manageGlossary);
    form->addRow(tr("Glossary"), glossaryRow);

    form->addRow(tr("Style"), new ConfigTextEdit(Keys::translationStyle, 3, page));
    form->addRow(tr("Background"), new ConfigTextEdit(Keys::translationBackground, 3, page));

    auto* autoTranslateCheck = new ConfigCheckBox(Keys::uiAutoTranslate, page);
    form->addRow(tr("Auto translate after typing"), autoTranslateCheck);
    form->addRow(tr("Auto translate delay (ms)"), new ConfigSpinBox(Keys::uiAutoTranslateDelay, 100, 10000, 100, page));
    return page;
}

QWidget* SettingsDialog::createInterfacePage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    auto* languageCombo = new ConfigComboBox(Keys::uiLanguage, page);
    languageCombo->setItems({
        {tr("Follow system"), QStringLiteral("auto")},
        {QStringLiteral("English"), QStringLiteral("en")},
        {QStringLiteral("简体中文"), QStringLiteral("zh")}
    });
    form->addRow(tr("Interface language"), languageCombo);

    auto* onTopCheck = new ConfigCheckBox(Keys::uiAlwaysOnTop, page);
    form->addRow(tr("Keep window on top"), onTopCheck);

    auto* trayCheck = new ConfigCheckBox(Keys::uiMinimizeToTray, page);
    form->addRow(tr("Minimize to tray on close"), trayCheck);

    form->addRow(tr("Font size"), new ConfigSpinBox(Keys::uiFontSize, 8, 24, 1, page));
    return page;
}

QWidget* SettingsDialog::createClipboardPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    auto* monitorCheck = new ConfigCheckBox(Keys::clipboardMonitor, page);
    form->addRow(tr("Monitor clipboard and translate automatically"), monitorCheck);
    form->addRow(tr("Monitor delay (ms)"), new ConfigSpinBox(Keys::clipboardDelayMs, 100, 5000, 50, page));
    return page;
}

QWidget* SettingsDialog::createHistoryPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    auto* enabledCheck = new ConfigCheckBox(Keys::historyEnabled, page);
    form->addRow(tr("Save translation history"), enabledCheck);
    form->addRow(tr("Max records"), new ConfigSpinBox(Keys::historyMaxRecords, 10, 100000, 10, page));

    auto* clearButton = new QPushButton(tr("Clear history now"), page);
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, tr("RiipL"), tr("Delete all history records?"))
            == QMessageBox::Yes)
            m_history->clear();
    });
    form->addRow(QString(), clearButton);
    return page;
}

QWidget* SettingsDialog::createPromptsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* contentRow = new QHBoxLayout();

    auto* list = new QListWidget(page);
    list->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    list->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);
    const QString uiLanguage = ConfigManager::instance()->resolvedUiLanguage();
    for (const TemplateInfo& info : templateInfos())
        list->addItem(uiLanguage == QLatin1String("zh") ? info.labelZh : info.labelEn);
    contentRow->addWidget(list);

    auto* stack = new QStackedWidget(page);
    for (const TemplateInfo& info : templateInfos()) {
        auto* pageWidget = new QWidget(stack);
        auto* pageLayout = new QVBoxLayout(pageWidget);
        auto* langTabs = new QTabWidget(pageWidget);
        auto* zhEditor = new ConfigTextEdit(Keys::promptKey(info.key, QStringLiteral("zh")), 10, pageWidget);
        auto* enEditor = new ConfigTextEdit(Keys::promptKey(info.key, QStringLiteral("en")), 10, pageWidget);
        langTabs->addTab(zhEditor, tr("Chinese template"));
        langTabs->addTab(enEditor, tr("English template"));
        pageLayout->addWidget(langTabs);

        auto* placeholderGroup = new QGroupBox(tr("Available placeholders (click to copy)"), pageWidget);
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
            if (placeholder == QLatin1String("translated_text")) return tr("Full translated text, available to the candidate wording prompt");
            if (placeholder == QLatin1String("selected_word")) return tr("Word the user selected in the translation");
            return QString();
        };
        for (const QString& placeholder : PromptBuilder::knownPlaceholders()) {
            const QString token = QLatin1Char('{') + placeholder + QLatin1Char('}');
            auto* chip = new QToolButton(placeholderGroup);
            chip->setText(token);
            chip->setToolTip(placeholderHint(placeholder));
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
    connect(list, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
    list->setCurrentRow(0);
    contentRow->addWidget(stack, 1);
    layout->addLayout(contentRow, 1);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    auto* testButton = new QPushButton(tr("Preview prompt..."), page);
    connect(testButton, &QPushButton::clicked, page, [this, page]() {
        PromptPreviewDialog dialog(
            m_targetLangCombo->box()->currentData().toString(),
            m_toneCombo->box()->currentData().toString(),
            m_glossaryEnabled->box()->isChecked(),
            Glossary::loadFromConfig().entries,
            page);
        dialog.exec();
    });
    buttonRow->addWidget(testButton);
    layout->addLayout(buttonRow);
    return page;
}

#include "SettingsDialog.moc"
