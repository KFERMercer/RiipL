#include "ConfigEditors.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/translation/Language.h"
#include "core/translation/Tone.h"
#include "ui/widgets/AppFonts.h"
#include "ui/widgets/AppIcons.h"

#include <QJsonObject>
#include <QLocale>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QToolButton>

namespace {

struct ConfigEditorsTr
{
    Q_DECLARE_TR_FUNCTIONS(ConfigEditors)
};

QToolButton* createResetButton(QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setIcon(AppIcons::reset());
    button->setVisible(false);
    return button;
}

}

ConfigEditor::ConfigEditor(const QString& key, QWidget* parent)
    : QWidget(parent)
    , m_key(key)
{
}

void ConfigEditor::setupDisplay(QToolButton* resetButton)
{
    m_reset = resetButton;
    retranslateUi();
    connect(m_reset, &QToolButton::clicked, this,
            [this]() { setUserValue(Defaults::value(m_key)); });
}

void ConfigEditor::captureBaseline()
{
    m_baseline = ConfigManager::instance()->value(m_key);
}

void ConfigEditor::loadConfigValue()
{
    m_guard = true;
    setControlValue(ConfigManager::instance()->value(m_key));
    m_guard = false;
    captureBaseline();
    refreshModifiedState();
}

bool ConfigEditor::isModified() const
{
    return value() != m_baseline;
}

void ConfigEditor::refreshBaseline()
{
    m_baseline = ConfigManager::instance()->value(m_key);
    refreshModifiedState();
}

void ConfigEditor::setUserValue(const QJsonValue& v)
{
    if (value() == v)
        return;
    m_guard = true;
    setControlValue(v);
    m_guard = false;
    refreshModifiedState();
    emit edited();
}

void ConfigEditor::handleControlChange()
{
    if (!m_guard)
        emit edited();
    refreshModifiedState();
}

void ConfigEditor::refreshModifiedState()
{
    if (!m_reset)
        return;
    m_reset->setVisible(value() != Defaults::value(m_key));
}

// The reset affordance tracks the default, while dirty tracking compares against
// the loaded baseline; the two differ when a stored value is out of range.
void ConfigEditor::retranslateUi()
{
    if (m_reset)
        m_reset->setToolTip(ConfigEditorsTr::tr("Reset to default"));
}

ConfigLineEdit::ConfigLineEdit(const QString& key, bool password, QWidget* parent)
    : ConfigEditor(key, parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_edit = new QLineEdit(this);
    if (password)
        m_edit->setEchoMode(QLineEdit::Password);
    auto* reset = createResetButton(this);
    layout->addWidget(m_edit, 1);
    layout->addWidget(reset);

    connect(m_edit, &QLineEdit::textEdited, this, [this]() { handleControlChange(); });
    setupDisplay(reset);
    loadConfigValue();
}

QJsonValue ConfigLineEdit::value() const
{
    return m_edit->text();
}

void ConfigLineEdit::setControlValue(const QJsonValue& v)
{
    m_edit->setText(v.toString());
}

ConfigComboBox::ConfigComboBox(const QString& key, QWidget* parent)
    : ConfigEditor(key, parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_box = new QComboBox(this);
    auto* reset = createResetButton(this);
    layout->addWidget(m_box);
    layout->addWidget(reset);
    layout->addStretch(1);

    connect(m_box, &QComboBox::currentIndexChanged, this, [this](int) { handleControlChange(); });
    setupDisplay(reset);
}

QJsonValue ConfigComboBox::value() const
{
    return m_box->currentData().toString();
}

void ConfigComboBox::setControlValue(const QJsonValue& v)
{
    const int index = m_box->findData(v.toString());
    m_box->setCurrentIndex(index < 0 ? 0 : index);
}

void ConfigComboBox::setItems(const QList<std::pair<QString, QString>>& items)
{
    const bool initialSelection = m_box->count() == 0;
    const QString wanted = initialSelection
        ? ConfigManager::instance()->value(key()).toString()
        : m_box->currentData().toString();

    QSignalBlocker blocker(m_box);
    m_box->clear();
    for (const std::pair<QString, QString>& item : items)
        m_box->addItem(item.first, item.second);

    int index = m_box->findData(wanted);
    if (index < 0)
        index = 0;
    m_box->setCurrentIndex(index);
    if (initialSelection)
        captureBaseline();
    refreshModifiedState();
    if (!initialSelection && m_box->currentData().toString() != wanted)
        handleControlChange();
}

class ConfigEditableComboBox::Box : public QComboBox
{
public:
    explicit Box(ConfigEditableComboBox* editor)
        : QComboBox(editor)
        , m_editor(editor)
    {
    }

    // Re-opens an open popup: it takes its height when shown, so candidates that
    // replaced the ones it opened with would stay out of sight.
    void refitPopup()
    {
        if (!view()->window()->isVisible())
            return;
        hidePopup();
        QComboBox::showPopup();
    }

protected:
    void showPopup() override
    {
        emit m_editor->popupAboutToShow();
        QComboBox::showPopup();
    }

private:
    ConfigEditableComboBox* m_editor;
};

ConfigEditableComboBox::ConfigEditableComboBox(const QString& key, QWidget* parent)
    : ConfigEditor(key, parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_box = new Box(this);
    m_box->setEditable(true);
    // A typed entry is a value of its own, not an entry to add to the list.
    m_box->setInsertPolicy(QComboBox::NoInsert);
    auto* reset = createResetButton(this);
    layout->addWidget(m_box, 1);
    layout->addWidget(reset);

    connect(m_box, &QComboBox::currentTextChanged, this,
            [this](const QString&) { handleControlChange(); });
    setupDisplay(reset);
    loadConfigValue();
}

QJsonValue ConfigEditableComboBox::value() const
{
    return m_box->currentText();
}

void ConfigEditableComboBox::setControlValue(const QJsonValue& v)
{
    selectText(v.toString());
}

void ConfigEditableComboBox::setItems(const QStringList& items)
{
    const QString current = m_box->currentText();
    {
        const QSignalBlocker blocker(m_box);
        m_box->clear();
        m_box->addItems(items);
        selectText(current);
    }
    m_box->refitPopup();
}

void ConfigEditableComboBox::setMessage(const QString& message)
{
    setItems({message});
    // The popup greys out what it reports rather than offering it as a choice.
    if (QStandardItemModel* model = qobject_cast<QStandardItemModel*>(m_box->model())) {
        if (QStandardItem* entry = model->item(0)) {
            entry->setEnabled(false);
            entry->setSelectable(false);
        }
    }
}

void ConfigEditableComboBox::selectText(const QString& text)
{
    const int index = m_box->findText(text);
    if (index >= 0) {
        m_box->setCurrentIndex(index);
        return;
    }
    // Nothing is selected, so a typed entry never highlights a candidate.
    m_box->setCurrentIndex(-1);
    m_box->setCurrentText(text);
}

ConfigTextEdit::ConfigTextEdit(const QString& key, QWidget* parent)
    : ConfigEditor(key, parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_edit = new QPlainTextEdit(this);
    auto* reset = createResetButton(this);
    layout->addWidget(m_edit, 1);
    layout->addWidget(reset);

    connect(m_edit, &QPlainTextEdit::textChanged, this, [this]() { handleControlChange(); });
    setupDisplay(reset);
    loadConfigValue();
}

QJsonValue ConfigTextEdit::value() const
{
    return m_edit->toPlainText();
}

void ConfigTextEdit::setControlValue(const QJsonValue& v)
{
    m_edit->setPlainText(v.toString());
}

void ConfigTextEdit::applyFixedWidthFont(QPlainTextEdit* edit)
{
    edit->setFont(AppFonts::fixedWidth());
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);
}

ConfigSpinBox::ConfigSpinBox(const QString& key, int minimum, int maximum, int step,
                             QWidget* parent)
    : ConfigEditor(key, parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_edit = new QSpinBox(this);
    m_edit->setRange(minimum, maximum);
    m_edit->setSingleStep(step);
    auto* reset = createResetButton(this);
    layout->addWidget(m_edit);
    layout->addWidget(reset);
    layout->addStretch(1);

    connect(m_edit, &QSpinBox::valueChanged, this, [this](int) { handleControlChange(); });
    setupDisplay(reset);
    loadConfigValue();
}

QJsonValue ConfigSpinBox::value() const
{
    return m_edit->value();
}

void ConfigSpinBox::setControlValue(const QJsonValue& v)
{
    m_edit->setValue(v.toInt());
}

ConfigDoubleSpinBox::ConfigDoubleSpinBox(const QString& key, double minimum, double maximum,
                                         double step, int decimals, QWidget* parent)
    : ConfigEditor(key, parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_edit = new QDoubleSpinBox(this);
    m_edit->setRange(minimum, maximum);
    m_edit->setSingleStep(step);
    m_edit->setDecimals(decimals);
    auto* reset = createResetButton(this);
    layout->addWidget(m_edit);
    layout->addWidget(reset);
    layout->addStretch(1);

    connect(m_edit, &QDoubleSpinBox::valueChanged, this, [this](double) { handleControlChange(); });
    setupDisplay(reset);
    loadConfigValue();
}

QJsonValue ConfigDoubleSpinBox::value() const
{
    return m_edit->value();
}

void ConfigDoubleSpinBox::setControlValue(const QJsonValue& v)
{
    m_edit->setValue(v.toDouble());
}

ConfigCheckBox::ConfigCheckBox(const QString& key, QWidget* parent)
    : ConfigEditor(key, parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_box = new QCheckBox(this);
    auto* reset = createResetButton(this);
    layout->addWidget(m_box);
    layout->addWidget(reset);
    layout->addStretch(1);

    connect(m_box, &QCheckBox::toggled, this, [this](bool) { handleControlChange(); });
    setupDisplay(reset);
    loadConfigValue();
}

QJsonValue ConfigCheckBox::value() const
{
    return m_box->isChecked();
}

void ConfigCheckBox::setControlValue(const QJsonValue& v)
{
    m_box->setChecked(v.toBool());
}

// The core tables mark their names with QT_TRANSLATE_NOOP under their own
// contexts, so the lookup goes through the matching one.
QString languageLabel(const QString& code)
{
    const char* source = Languages::labelFor(code);
    return source ? QCoreApplication::translate("Languages", source) : code;
}

QString toneLabel(const QString& key)
{
    const char* source = Tones::labelFor(key);
    return source ? QCoreApplication::translate("Tones", source) : key;
}

QList<std::pair<QString, QString>> uiLanguageItems()
{
    QList<std::pair<QString, QString>> items;
    items.append({ConfigEditorsTr::tr("Follow system"), Keys::uiLanguageAuto});
    // A language list names each language in itself, which is what a reader
    // looking for their own language recognises. The World country drops the
    // region, so English reads as "English" rather than "American English".
    for (const QString& code : Keys::uiLanguageCodes()) {
        const QLocale locale(code);
        items.append({QLocale(locale.language(), QLocale::World).nativeLanguageName(), code});
    }
    return items;
}

QList<std::pair<QString, QString>> languageItems(bool includeAuto)
{
    QList<std::pair<QString, QString>> items;
    for (const LangItem& lang : Languages::all()) {
        if (!includeAuto && lang.code == QLatin1String("auto"))
            continue;
        items.append({languageLabel(lang.code), lang.code});
    }
    return items;
}

QList<std::pair<QString, QString>> toneItems(const QJsonArray& customTones)
{
    QList<std::pair<QString, QString>> items;
    for (const ToneItem& tone : Tones::presets())
        items.append({toneLabel(tone.key), tone.key});
    for (const QJsonValue& value : customTones) {
        const QJsonObject object = value.toObject();
        const QString key = object.value(QStringLiteral("key")).toString();
        items.append({object.value(QStringLiteral("name")).toString(key), key});
    }
    return items;
}

void selectComboItem(QComboBox* box, const QString& key)
{
    const int index = box->findData(key);
    box->setCurrentIndex(index < 0 ? 0 : index);
}
