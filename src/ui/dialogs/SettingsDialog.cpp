#include "SettingsDialog.h"

#include "GlossaryDialog.h"
#include "ToneDialog.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/history/HistoryManager.h"
#include "core/models/Glossary.h"
#include "core/translation/Language.h"
#include "core/translation/PromptBuilder.h"
#include "core/translation/Tone.h"
#include "platform/GlobalHotkey.h"
#include "ui/widgets/ConfigEditors.h"
#include "utils/JsonUtils.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
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
        {QStringLiteral("default"), QStringLiteral("Default"), QStringLiteral("默认指令")},
        {QStringLiteral("system"), QStringLiteral("System prompt"), QStringLiteral("系统提示词")},
        {QStringLiteral("glossary"), QStringLiteral("Glossary"), QStringLiteral("术语表")},
        {QStringLiteral("tone"), QStringLiteral("Tone"), QStringLiteral("语气")},
        {QStringLiteral("style"), QStringLiteral("Style"), QStringLiteral("风格")},
        {QStringLiteral("background"), QStringLiteral("Background"), QStringLiteral("背景信息")},
        {QStringLiteral("personalization"), QStringLiteral("Personalization"), QStringLiteral("个性化偏好")},
        {QStringLiteral("candidate"), QStringLiteral("Candidate wording"), QStringLiteral("候选遣词")}
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
        resize(680, 560);
        auto* layout = new QVBoxLayout(this);

        auto* form = new QFormLayout();
        m_source = new QPlainTextEdit(this);
        m_source->setFixedHeight(72);
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

SettingsDialog::SettingsDialog(GlobalHotkey* hotkey, int initialTab, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    resize(760, 620);

    m_customTones = ConfigManager::instance()->value(Keys::translationCustomTones).toArray();

    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    tabs->addTab(createApiPage(), tr("API"));
    tabs->addTab(createTranslationPage(), tr("Translation"));
    tabs->addTab(createGlossaryPage(), tr("Glossary"));
    tabs->addTab(createPromptsPage(), tr("Prompt templates"));
    tabs->addTab(createInterfacePage(), tr("Interface"));
    tabs->addTab(createHotkeyPage(), tr("Hotkey"));
    if (hotkey && !hotkey->isSupported())
        tabs->setTabToolTip(tabs->indexOf(tabs->widget(5)), tr("Global hotkeys are limited on this platform"));
    tabs->addTab(createClipboardPage(), tr("Clipboard"));
    tabs->addTab(createHistoryPage(), tr("History"));
    layout->addWidget(tabs, 1);
    if (initialTab >= 0 && initialTab < tabs->count())
        tabs->setCurrentIndex(initialTab);

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

    for (ConfigEditor* editor : findChildren<ConfigEditor*>())
        connect(editor, &ConfigEditor::edited, this, &SettingsDialog::markDirty);
    connect(m_hotkeySequence, &QKeySequenceEdit::keySequenceChanged, this, &SettingsDialog::markDirty);
    connect(m_glossaryTable, &GlossaryTable::entriesChanged, this, &SettingsDialog::markDirty);
}

void SettingsDialog::markDirty()
{
    m_applyButton->setEnabled(true);
}

void SettingsDialog::applyChanges()
{
    ConfigManager* config = ConfigManager::instance();

    const QString sequence = m_hotkeySequence->keySequence().toString(QKeySequence::PortableText);
    if (sequence != config->stringValue(Keys::hotkeySequence))
        config->setValue(Keys::hotkeySequence, sequence);

    if (!JsonUtils::equals(m_customTones, config->value(Keys::translationCustomTones)))
        config->setValue(Keys::translationCustomTones, m_customTones);

    const QJsonArray entries = Glossary::toJson(m_glossaryTable->entries());
    if (!JsonUtils::equals(entries, config->value(Keys::glossaryEntries)))
        config->setValue(Keys::glossaryEntries, entries);

    for (ConfigEditor* editor : findChildren<ConfigEditor*>()) {
        const QJsonValue editorValue = editor->value();
        if (!JsonUtils::equals(editorValue, config->value(editor->key())))
            config->setValue(editor->key(), editorValue);
    }

    m_applyButton->setEnabled(false);
}

QWidget* SettingsDialog::createApiPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->addRow(tr("Base URL"), new ConfigLineEdit(Keys::apiBaseUrl, false, page));
    form->addRow(tr("API key"), new ConfigLineEdit(Keys::apiKey, true, page));
    form->addRow(tr("Model"), new ConfigLineEdit(Keys::apiModel, false, page));
    form->addRow(tr("Temperature"), new ConfigDoubleSpinBox(Keys::apiTemperature, 0.0, 2.0, 0.1, 2, page));
    form->addRow(tr("Max tokens"), new ConfigSpinBox(Keys::apiMaxTokens, 1, 1000000, 256, page));
    form->addRow(tr("Top P"), new ConfigDoubleSpinBox(Keys::apiTopP, 0.0, 1.0, 0.05, 2, page));
    auto* streamCheck = new ConfigCheckBox(Keys::apiStream, page);
    streamCheck->box()->setText(tr("Stream responses"));
    form->addRow(QString(), streamCheck);

    auto* extraEdit = new ConfigTextEdit(Keys::apiExtraBody, 4, page);
    auto* validation = new QLabel(page);
    auto updateValidation = [extraEdit, validation]() {
        const QString text = extraEdit->edit()->toPlainText().trimmed();
        if (text.isEmpty()) {
            validation->setText(tr("Empty: no extra parameters"));
            validation->setStyleSheet(QStringLiteral("color: gray;"));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
        if (doc.isObject()) {
            validation->setText(QStringLiteral("\u2713 ") + tr("Valid JSON object"));
            validation->setStyleSheet(QStringLiteral("color: green;"));
        } else {
            validation->setText(QStringLiteral("\u2717 ") + tr("Invalid JSON: an object with key-value pairs is expected"));
            validation->setStyleSheet(QStringLiteral("color: red;"));
        }
    };
    connect(extraEdit->edit(), &QPlainTextEdit::textChanged, page, updateValidation);
    form->addRow(tr("Extra body (JSON)"), extraEdit);
    form->addRow(QString(), validation);
    updateValidation();
    return page;
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
            markDirty();
        }
    });
    toneRow->addWidget(m_toneCombo, 1);
    toneRow->addWidget(manageTones);
    form->addRow(tr("Tone"), toneRow);

    form->addRow(tr("Style"), new ConfigTextEdit(Keys::translationStyle, 3, page));
    form->addRow(tr("Background"), new ConfigTextEdit(Keys::translationBackground, 3, page));

    auto* autoTranslateCheck = new ConfigCheckBox(Keys::uiAutoTranslate, page);
    autoTranslateCheck->box()->setText(tr("Auto translate after typing"));
    form->addRow(QString(), autoTranslateCheck);
    form->addRow(tr("Auto translate delay (ms)"), new ConfigSpinBox(Keys::uiAutoTranslateDelay, 100, 10000, 100, page));
    return page;
}

QWidget* SettingsDialog::createGlossaryPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    m_glossaryEnabled = new ConfigCheckBox(Keys::glossaryEnabled, page);
    m_glossaryEnabled->box()->setText(tr("Enable glossary"));
    layout->addWidget(m_glossaryEnabled);

    m_glossaryTable = new GlossaryTable(page);
    m_glossaryTable->setEntries(Glossary::loadFromConfig().entries);
    layout->addWidget(m_glossaryTable, 1);
    return page;
}

QWidget* SettingsDialog::createPromptsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QHBoxLayout(page);

    auto* list = new QListWidget(page);
    list->setFixedWidth(170);
    const QString uiLanguage = ConfigManager::instance()->resolvedUiLanguage();
    for (const TemplateInfo& info : templateInfos())
        list->addItem(uiLanguage == QLatin1String("zh") ? info.labelZh : info.labelEn);
    layout->addWidget(list);

    auto* stack = new QStackedWidget(page);
    for (const TemplateInfo& info : templateInfos()) {
        auto* pageWidget = new QWidget(stack);
        auto* pageLayout = new QVBoxLayout(pageWidget);
        auto* langTabs = new QTabWidget(pageWidget);
        auto* zhEditor = new ConfigTextEdit(QStringLiteral("prompts.%1_zh").arg(info.key), 10, pageWidget);
        auto* enEditor = new ConfigTextEdit(QStringLiteral("prompts.%1_en").arg(info.key), 10, pageWidget);
        langTabs->addTab(zhEditor, tr("Chinese template"));
        langTabs->addTab(enEditor, tr("English template"));
        pageLayout->addWidget(langTabs);

        auto* hint = new QLabel(tr("Available placeholders: {source_text} {target_lang} {source_lang} {tone} {target_style} {background_text} {glossary} {user_preferences} {word} {translated_text}"), pageWidget);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color: gray;"));
        pageLayout->addWidget(hint);
        stack->addWidget(pageWidget);
    }
    connect(list, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
    list->setCurrentRow(0);
    layout->addWidget(stack, 1);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    auto* testButton = new QPushButton(tr("Preview prompt..."), page);
    connect(testButton, &QPushButton::clicked, page, [this, page]() {
        PromptPreviewDialog dialog(
            m_targetLangCombo->box()->currentData().toString(),
            m_toneCombo->box()->currentData().toString(),
            m_glossaryEnabled->box()->isChecked(),
            m_glossaryTable->entries(),
            page);
        dialog.exec();
    });
    buttonRow->addWidget(testButton);
    layout->addLayout(buttonRow);
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
    onTopCheck->box()->setText(tr("Keep window on top"));
    form->addRow(QString(), onTopCheck);

    auto* trayCheck = new ConfigCheckBox(Keys::uiMinimizeToTray, page);
    trayCheck->box()->setText(tr("Minimize to tray on close"));
    form->addRow(QString(), trayCheck);

    form->addRow(tr("Font size"), new ConfigSpinBox(Keys::uiFontSize, 8, 24, 1, page));
    return page;
}

QWidget* SettingsDialog::createHotkeyPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    auto* enabledCheck = new ConfigCheckBox(Keys::hotkeyEnabled, page);
    enabledCheck->box()->setText(tr("Enable global hotkey"));
    form->addRow(QString(), enabledCheck);

    m_hotkeySequence = new QKeySequenceEdit(QKeySequence(ConfigManager::instance()->stringValue(Keys::hotkeySequence),
                                                         QKeySequence::PortableText), page);
    form->addRow(tr("Hotkey sequence"), m_hotkeySequence);

    auto* hint = new QLabel(tr("Requires Ctrl/Alt/Meta modifiers. On Wayland, global hotkeys may not work."), page);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: gray;"));
    form->addRow(QString(), hint);
    return page;
}

QWidget* SettingsDialog::createClipboardPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    auto* monitorCheck = new ConfigCheckBox(Keys::clipboardMonitor, page);
    monitorCheck->box()->setText(tr("Monitor clipboard and translate automatically"));
    form->addRow(QString(), monitorCheck);
    form->addRow(tr("Monitor delay (ms)"), new ConfigSpinBox(Keys::clipboardDelayMs, 100, 5000, 50, page));
    return page;
}

QWidget* SettingsDialog::createHistoryPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    auto* enabledCheck = new ConfigCheckBox(Keys::historyEnabled, page);
    enabledCheck->box()->setText(tr("Save translation history"));
    form->addRow(QString(), enabledCheck);
    form->addRow(tr("Max records"), new ConfigSpinBox(Keys::historyMaxRecords, 10, 100000, 10, page));

    auto* clearButton = new QPushButton(tr("Clear history now"), page);
    connect(clearButton, &QPushButton::clicked, page, []() {
        if (QMessageBox::question(nullptr, tr("RiipL"), tr("Delete all history records?"))
            == QMessageBox::Yes) {
            HistoryManager manager(ConfigManager::instance()->historyFilePath());
            manager.setMaxRecords(ConfigManager::instance()->intValue(Keys::historyMaxRecords));
            manager.clear();
        }
    });
    form->addRow(QString(), clearButton);
    return page;
}

#include "SettingsDialog.moc"
