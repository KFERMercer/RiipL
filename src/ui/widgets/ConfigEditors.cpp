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
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QToolButton>

namespace {

struct ConfigEditorsTr
{
    Q_DECLARE_TR_FUNCTIONS(ConfigEditors)
};

// Label of a candidate.
constexpr int kLabelRole = Qt::UserRole + 1;

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

// Draws a candidate with its label after the entry.
class ConfigEditableComboBox::LabelDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        const QString label = index.data(kLabelRole).toString();
        if (label.isEmpty()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        QStyleOptionViewItem row(opt);
        row.text.clear();
        QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &row, painter, opt.widget);

        const QFontMetrics metrics(opt.fontMetrics);
        const int space = metrics.horizontalAdvance(QChar::Space);
        const int gap = 2 * space;
        const QRect textRect = opt.rect.adjusted(space, 0, -space, 0);
        const bool enabled = opt.state & QStyle::State_Enabled;

        const QString entry = metrics.elidedText(
            opt.text, Qt::ElideRight,
            qMax(0, textRect.width() - metrics.horizontalAdvance(label) - gap));
        style->drawItemText(painter, textRect, Qt::AlignLeft | Qt::AlignVCenter, opt.palette,
                            enabled, entry, QPalette::Text);

        QPalette labelPalette = opt.palette;
        labelPalette.setColor(QPalette::Text, labelPalette.color(QPalette::PlaceholderText));
        QRect labelRect = textRect;
        labelRect.setLeft(textRect.left() + metrics.horizontalAdvance(entry) + gap);
        style->drawItemText(painter, labelRect, Qt::AlignLeft | Qt::AlignVCenter, labelPalette,
                            enabled, label, QPalette::Text);
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        const QString label = index.data(kLabelRole).toString();
        if (label.isEmpty())
            return size;
        // The popup sizes to its widest row, so the label and the space it takes
        // add to the width the entry asks for.
        const QFontMetrics metrics(option.font);
        return size + QSize(metrics.horizontalAdvance(label) + 4 * metrics.horizontalAdvance(QChar::Space), 0);
    }
};

class ConfigEditableComboBox::Box : public QComboBox
{
public:
    explicit Box(ConfigEditableComboBox* editor)
        : QComboBox(editor)
        , m_editor(editor)
    {
        setEditable(true);
        // A typed entry is a value of its own, not an entry to add to the list.
        setInsertPolicy(QComboBox::NoInsert);

        completer()->setCompletionMode(QCompleter::PopupCompletion);
        completer()->setFilterMode(Qt::MatchContains);
        completer()->popup()->setItemDelegate(new LabelDelegate(completer()->popup()));
        // A wrapping list drops the selection past its last row, which the field
        // reads as an empty value.
        completer()->setWrapAround(false);

        // Typing turns the field text into a search; an emptied field dismisses the
        // list instead.
        connect(lineEdit(), &QLineEdit::textEdited, this, [this] {
            if (!currentText().isEmpty())
                present();
        });
    }

    // Lists the rows that replace the ones the popup opened with. A list the user
    // closed stays closed, and the value stays selected for the next keystroke.
    void refreshCandidates()
    {
        m_report = false;
        if (!completer()->popup()->isVisible())
            return;
        present();
        if (currentText() == m_value)
            lineEdit()->selectAll();
    }

    // Lists a row that reports something the list cannot offer instead of candidates.
    void refreshReport()
    {
        m_report = true;
        if (completer()->popup()->isVisible())
            present();
    }

protected:
    void showPopup() override
    {
        if (completer()->popup()->isVisible()) {
            completer()->popup()->hide();
            return;
        }
        m_value = currentText();
        emit m_editor->popupAboutToShow();
        lineEdit()->setFocus();
        lineEdit()->selectAll();
        present();
    }

private:
    // Lists the rows whole unless the field holds a search over the candidates.
    void present()
    {
        QCompleter* search = completer();
        const bool whole = m_report || currentText() == m_value;
        search->setCompletionMode(whole ? QCompleter::UnfilteredPopupCompletion
                                        : QCompleter::PopupCompletion);
        // Qt writes the prefix back into the field, so the prefix is what it holds.
        search->setCompletionPrefix(currentText());
        QAbstractItemView* popup = search->popup();
        // The popup is only as wide as the field, which the widest row with its label
        // does not fit in.
        popup->setMinimumWidth(popup->sizeHintForColumn(0) + 2 * popup->frameWidth());
        search->complete();
    }

    ConfigEditableComboBox* m_editor;
    // Text the field held when the list opened: any other text in it is a search.
    QString m_value;
    // The rows report something the list cannot offer instead of candidates.
    bool m_report = false;
};

ConfigEditableComboBox::ConfigEditableComboBox(const QString& key, QWidget* parent)
    : ConfigEditor(key, parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_box = new Box(this);
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

void ConfigEditableComboBox::setItems(const QList<std::pair<QString, QString>>& items)
{
    replaceItems(items);
    m_box->refreshCandidates();
}

void ConfigEditableComboBox::setMessage(const QString& message)
{
    replaceItems({{message, QString()}});
    // The popup greys out what it reports rather than offering it as a choice.
    if (QStandardItemModel* model = qobject_cast<QStandardItemModel*>(m_box->model())) {
        if (QStandardItem* entry = model->item(0))
            entry->setEnabled(false);
    }
    m_box->refreshReport();
}

void ConfigEditableComboBox::replaceItems(const QList<std::pair<QString, QString>>& items)
{
    const QString current = m_box->currentText();
    const QSignalBlocker blocker(m_box);
    m_box->clear();
    for (const std::pair<QString, QString>& item : items) {
        m_box->addItem(item.first);
        m_box->setItemData(m_box->count() - 1, item.second, kLabelRole);
    }
    selectText(current);
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
QString languageLabel(const QString& value, const QVector<CustomLanguage>& custom)
{
    if (const char* source = Languages::labelFor(value))
        return QCoreApplication::translate("Languages", source);
    for (const CustomLanguage& language : custom) {
        if (language.key == value)
            return language.name;
    }
    return value;
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

QList<std::pair<QString, QString>> languageItems(bool includeAuto,
                                                 const QVector<CustomLanguage>& custom)
{
    QList<std::pair<QString, QString>> items;
    for (const LangItem& lang : Languages::all()) {
        if (!includeAuto && lang.code == QLatin1String("auto"))
            continue;
        items.append({languageLabel(lang.code), lang.code});
    }
    for (const CustomLanguage& language : custom) {
        // A built-in code already resolves to its catalog name.
        if (Languages::indexOf(language.key) < 0)
            items.append({language.name, language.key});
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
