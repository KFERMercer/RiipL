#include "ConfigEditors.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "ui/widgets/AppIcons.h"
#include "utils/JsonUtils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

struct ConfigEditorsTr
{
    Q_DECLARE_TR_FUNCTIONS(ConfigEditors)
};

QToolButton* createResetButton(QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setIcon(AppIcons::reset());
    button->setToolTip(ConfigEditorsTr::tr("Reset to default"));
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
    connect(m_reset, &QToolButton::clicked, this, [this]() {
        m_guard = true;
        setControlValue(Defaults::value(m_key));
        m_guard = false;
        refreshModifiedState();
        emit edited();
    });
}

void ConfigEditor::loadConfigValue()
{
    m_guard = true;
    setControlValue(ConfigManager::instance()->value(m_key));
    m_guard = false;
    refreshModifiedState();
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
    const bool modified = !JsonUtils::equals(value(), Defaults::value(m_key));
    m_reset->setVisible(modified);
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

void ConfigComboBox::setItems(const QList<QPair<QString, QString>>& items)
{
    const bool initialSelection = m_box->count() == 0;
    const QString wanted = initialSelection
        ? ConfigManager::instance()->value(key()).toString()
        : m_box->currentData().toString();

    QSignalBlocker blocker(m_box);
    m_box->clear();
    for (const QPair<QString, QString>& item : items)
        m_box->addItem(item.first, item.second);

    int index = m_box->findData(wanted);
    if (index < 0)
        index = 0;
    m_box->setCurrentIndex(index);
    refreshModifiedState();
}

ConfigTextEdit::ConfigTextEdit(const QString& key, int rows, QWidget* parent)
    : ConfigEditor(key, parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_edit = new QPlainTextEdit(this);
    m_edit->setMinimumHeight(rows * m_edit->fontMetrics().lineSpacing()
                             + 2 * m_edit->frameWidth()
                             + 2 * m_edit->document()->documentMargin());
    auto* bottomRow = new QHBoxLayout();
    bottomRow->addStretch(1);
    auto* reset = createResetButton(this);
    bottomRow->addWidget(reset);
    layout->addWidget(m_edit, 1);
    layout->addLayout(bottomRow);

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
