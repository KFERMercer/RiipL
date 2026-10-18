#include "PromptPreviewDialog.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/models/Glossary.h"
#include "core/translation/Language.h"
#include "core/translation/PromptBuilder.h"
#include "ui/widgets/AppFonts.h"
#include "ui/widgets/ConfigEditors.h"
#include "ui/widgets/TranslationEdit.h"
#include "ui/widgets/WindowState.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QtMath>

#include <utility>

namespace {

constexpr int kInputLines = 4;

}

PromptPreviewDialog::PromptPreviewDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Prompt preview"));
    ConfigManager* config = ConfigManager::instance();
    m_glossaryEnabled = config->boolValue(Keys::glossaryEnabled);
    m_glossary = Glossary::loadFromConfig().entries;
    const QVector<CustomLanguage> custom = Languages::customFromJson(
        config->value(Keys::translationCustomLanguages).toArray());

    auto* layout = new QVBoxLayout(this);

    auto* form = new QFormLayout();
    m_source = createTextBox();
    m_source->setPlainText(QStringLiteral("Hello, world!"));
    m_sourceLang = new QComboBox(this);
    for (const std::pair<QString, QString>& item : languageItems(true, custom))
        m_sourceLang->addItem(item.first, item.second);
    selectComboItem(m_sourceLang, config->stringValue(Keys::translationSourceLang),
                    Defaults::value(Keys::translationSourceLang).toString());
    m_target = new QComboBox(this);
    for (const std::pair<QString, QString>& item : languageItems(false, custom))
        m_target->addItem(item.first, item.second);
    selectComboItem(m_target, config->stringValue(Keys::translationTargetLang),
                    Defaults::value(Keys::translationTargetLang).toString());
    m_tone = new QComboBox(this);
    for (const std::pair<QString, QString>& item
         : toneItems(config->value(Keys::translationCustomTones).toArray())) {
        m_tone->addItem(item.first, item.second);
    }
    selectComboItem(m_tone, config->stringValue(Keys::translationTone),
                    Defaults::value(Keys::translationTone).toString());
    m_style = createTextBox();
    m_style->setPlainText(config->stringValue(Keys::translationStyle));
    m_background = createTextBox();
    m_background->setPlainText(config->stringValue(Keys::translationBackground));
    form->addRow(tr("Text to translate"), m_source);
    form->addRow(tr("Source language"), m_sourceLang);
    form->addRow(tr("Target language"), m_target);
    form->addRow(tr("Tone"), m_tone);
    form->addRow(tr("Style"), m_style);
    form->addRow(tr("Background"), m_background);
    layout->addLayout(form);

    auto* previewGroup = new QGroupBox(tr("Preview"), this);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    auto* previewForm = new QFormLayout();
    // The role names the model reads the two prompts under, so they stay English.
    m_system = createPreviewPane();
    m_user = createPreviewPane();
    previewForm->addRow(QStringLiteral("System"), m_system);
    previewForm->addRow(QStringLiteral("User"), m_user);
    previewLayout->addLayout(previewForm);
    layout->addWidget(previewGroup, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    connect(m_source, &QTextEdit::textChanged, this, &PromptPreviewDialog::refresh);
    connect(m_sourceLang, &QComboBox::currentIndexChanged, this, &PromptPreviewDialog::refresh);
    connect(m_target, &QComboBox::currentIndexChanged, this, &PromptPreviewDialog::refresh);
    connect(m_tone, &QComboBox::currentIndexChanged, this, &PromptPreviewDialog::refresh);
    connect(m_style, &QTextEdit::textChanged, this, &PromptPreviewDialog::refresh);
    connect(m_background, &QTextEdit::textChanged, this, &PromptPreviewDialog::refresh);
    refresh();
    WindowState::track(this, WindowState::Id::promptPreview);
}

QTextEdit* PromptPreviewDialog::createTextBox()
{
    auto* view = new QTextEdit(this);
    // Plain text only, so a paste carries no formatting into the prompt.
    view->setAcceptRichText(false);
    view->setFixedHeight(kInputLines * view->fontMetrics().lineSpacing()
                         + 2 * qRound(view->document()->documentMargin())
                         + 2 * view->frameWidth());
    return view;
}

TranslationEdit* PromptPreviewDialog::createPreviewPane()
{
    auto* view = new TranslationEdit(this);
    view->setFont(AppFonts::fixedWidth());
    view->setWordSelectionEnabled(false);
    view->setPlaceholderText(tr("(empty prompt)"));
    return view;
}

void PromptPreviewDialog::refresh()
{
    TranslationContext context;
    context.sourceText = m_source->toPlainText();
    context.sourceLang = m_sourceLang->currentData().toString();
    context.targetLang = m_target->currentData().toString();
    context.tone = m_tone->currentData().toString();
    context.style = m_style->toPlainText();
    context.background = m_background->toPlainText();
    context.glossaryEnabled = m_glossaryEnabled;
    context.glossary = m_glossary;
    const PromptBuilder::Result result = PromptBuilder::build(context);
    // Replacing the text also puts the reader back at the top of the prompt.
    m_system->setResult(result.system);
    m_user->setResult(result.user);
}
