#include "PromptPreviewDialog.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/models/Glossary.h"
#include "core/translation/PromptBuilder.h"
#include "ui/widgets/AppFonts.h"
#include "ui/widgets/ConfigEditors.h"
#include "ui/widgets/WindowState.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>

PromptPreviewDialog::PromptPreviewDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Prompt preview"));
    ConfigManager* config = ConfigManager::instance();
    m_glossaryEnabled = config->boolValue(Keys::glossaryEnabled);
    m_glossary = Glossary::loadFromConfig().entries;

    auto* layout = new QVBoxLayout(this);

    auto* form = new QFormLayout();
    m_source = new QLineEdit(this);
    m_source->setText(QStringLiteral("Hello, world! RiipL is a translation tool."));
    m_sourceLang = new QComboBox(this);
    for (const QPair<QString, QString>& item : languageItems(true))
        m_sourceLang->addItem(item.first, item.second);
    selectComboItem(m_sourceLang, config->stringValue(Keys::translationSourceLang));
    m_target = new QComboBox(this);
    for (const QPair<QString, QString>& item : languageItems(false))
        m_target->addItem(item.first, item.second);
    selectComboItem(m_target, config->stringValue(Keys::translationTargetLang));
    m_tone = new QComboBox(this);
    for (const QPair<QString, QString>& item
         : toneItems(config->value(Keys::translationCustomTones).toArray())) {
        m_tone->addItem(item.first, item.second);
    }
    selectComboItem(m_tone, config->stringValue(Keys::translationTone));
    m_style = new QLineEdit(config->stringValue(Keys::translationStyle), this);
    m_background = new QLineEdit(config->stringValue(Keys::translationBackground), this);
    form->addRow(tr("Sample text"), m_source);
    form->addRow(tr("Source language"), m_sourceLang);
    form->addRow(tr("Target language"), m_target);
    form->addRow(tr("Tone"), m_tone);
    form->addRow(tr("Style"), m_style);
    form->addRow(tr("Background"), m_background);
    layout->addLayout(form);

    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setFont(AppFonts::fixedWidth());
    layout->addWidget(m_output, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    connect(m_source, &QLineEdit::textChanged, this, &PromptPreviewDialog::refresh);
    connect(m_sourceLang, &QComboBox::currentIndexChanged, this, &PromptPreviewDialog::refresh);
    connect(m_target, &QComboBox::currentIndexChanged, this, &PromptPreviewDialog::refresh);
    connect(m_tone, &QComboBox::currentIndexChanged, this, &PromptPreviewDialog::refresh);
    connect(m_style, &QLineEdit::textChanged, this, &PromptPreviewDialog::refresh);
    connect(m_background, &QLineEdit::textChanged, this, &PromptPreviewDialog::refresh);
    refresh();
    WindowState::track(this, WindowState::Id::promptPreview);
}

void PromptPreviewDialog::refresh()
{
    TranslationContext context;
    context.sourceText = m_source->text();
    context.sourceLang = m_sourceLang->currentData().toString();
    context.targetLang = m_target->currentData().toString();
    context.tone = m_tone->currentData().toString();
    context.style = m_style->text().trimmed();
    context.background = m_background->text().trimmed();
    context.glossaryEnabled = m_glossaryEnabled;
    context.glossary = m_glossary;
    const PromptBuilder::Result result = PromptBuilder::build(context);
    QString text;
    if (!result.system.isEmpty())
        text += QStringLiteral("[system]\n%1\n\n").arg(result.system);
    text += result.user;
    m_output->setPlainText(text.isEmpty() ? tr("(empty prompt)") : text);
}
