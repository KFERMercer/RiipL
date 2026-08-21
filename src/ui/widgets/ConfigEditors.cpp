#include "ConfigEditors.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "ui/widgets/AppIcons.h"

#include <QApplication>
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

void markModified(QWidget* widget, bool modified)
{
    QFont font = widget->font();
    font.setBold(modified);
    widget->setFont(font);
}

}

ConfigLineEdit::ConfigLineEdit(const QString& key, bool password, QWidget* parent)
    : QWidget(parent)
    , m_key(key)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_edit = new QLineEdit(this);
    if (password)
        m_edit->setEchoMode(QLineEdit::Password);
    m_reset = createResetButton(this);
    layout->addWidget(m_edit, 1);
    layout->addWidget(m_reset);

    connect(m_reset, &QToolButton::clicked, this, [this]() {
        ConfigManager::instance()->removeValue(m_key);
        m_guard = true;
        m_edit->clear();
        m_guard = false;
        applyState();
    });
    connect(m_edit, &QLineEdit::textEdited, this, [this](const QString& text) {
        ConfigManager::instance()->setValue(m_key, text);
        applyState();
    });
    connect(ConfigManager::instance(), &ConfigManager::changed,
            this, &ConfigLineEdit::syncFromConfig);

    m_guard = true;
    m_edit->setText(ConfigManager::instance()->stringValue(m_key));
    m_edit->setPlaceholderText(ConfigManager::instance()->placeholderFor(m_key));
    m_guard = false;
    applyState();
}

void ConfigLineEdit::syncFromConfig(const QString& changedKey)
{
    if (changedKey != m_key)
        return;
    m_guard = true;
    m_edit->setText(ConfigManager::instance()->stringValue(m_key));
    m_guard = false;
    applyState();
}

void ConfigLineEdit::applyState()
{
    const bool modified = !ConfigManager::instance()->isDefault(m_key);
    m_reset->setVisible(modified);
    markModified(m_edit, modified);
}

ConfigComboBox::ConfigComboBox(const QString& key, QWidget* parent)
    : QWidget(parent)
    , m_key(key)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_box = new QComboBox(this);
    m_reset = createResetButton(this);
    layout->addWidget(m_box);
    layout->addWidget(m_reset);
    layout->addStretch(1);

    connect(m_reset, &QToolButton::clicked, this, [this]() {
        ConfigManager::instance()->removeValue(m_key);
    });
    connect(m_box, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_guard || index < 0)
            return;
        ConfigManager::instance()->setValue(m_key, m_box->itemData(index).toString());
        applyState();
    });
    connect(ConfigManager::instance(), &ConfigManager::changed,
            this, &ConfigComboBox::onConfigChanged);

    reload();
}

void ConfigComboBox::onConfigChanged(const QString& key)
{
    if (key == m_key)
        reload();
}

void ConfigComboBox::reload()
{
    const QString current = ConfigManager::instance()->value(m_key).toString();
    const int index = m_box->findData(current);
    m_guard = true;
    m_box->setCurrentIndex(index < 0 ? 0 : index);
    m_guard = false;
    applyState();
}

void ConfigComboBox::setItems(const QList<QPair<QString, QString>>& items)
{
    QSignalBlocker blocker(m_box);
    m_box->clear();
    for (const QPair<QString, QString>& item : items)
        m_box->addItem(item.first, item.second);
    reload();
}

void ConfigComboBox::applyState()
{
    const bool modified = !ConfigManager::instance()->isDefault(m_key);
    m_reset->setVisible(modified);
    markModified(m_box, modified);
}

ConfigTextEdit::ConfigTextEdit(const QString& key, int rows, QWidget* parent)
    : QWidget(parent)
    , m_key(key)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_edit = new QPlainTextEdit(this);
    const int lineHeight = m_edit->fontMetrics().height();
    m_edit->setMinimumHeight(rows * lineHeight + 12);
    auto* bottomRow = new QHBoxLayout();
    bottomRow->addStretch(1);
    m_reset = createResetButton(this);
    bottomRow->addWidget(m_reset);
    layout->addWidget(m_edit, 1);
    layout->addLayout(bottomRow);

    connect(m_reset, &QToolButton::clicked, this, [this]() {
        ConfigManager::instance()->removeValue(m_key);
        m_guard = true;
        m_edit->clear();
        m_guard = false;
        applyState();
    });
    connect(m_edit, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_guard)
            return;
        ConfigManager::instance()->setValue(m_key, m_edit->toPlainText());
        applyState();
    });
    connect(ConfigManager::instance(), &ConfigManager::changed,
            this, &ConfigTextEdit::syncFromConfig);

    m_guard = true;
    m_edit->setPlainText(ConfigManager::instance()->stringValue(m_key));
    m_edit->setPlaceholderText(ConfigManager::instance()->placeholderFor(m_key));
    m_guard = false;
    applyState();
}

void ConfigTextEdit::syncFromConfig(const QString& changedKey)
{
    if (changedKey != m_key)
        return;
    m_guard = true;
    m_edit->setPlainText(ConfigManager::instance()->stringValue(m_key));
    m_guard = false;
    applyState();
}

void ConfigTextEdit::applyState()
{
    const bool modified = !ConfigManager::instance()->isDefault(m_key);
    m_reset->setVisible(modified);
    markModified(m_edit, modified);
}

ConfigSpinBox::ConfigSpinBox(const QString& key, int minimum, int maximum, int step,
                             QWidget* parent)
    : QWidget(parent)
    , m_key(key)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_edit = new QSpinBox(this);
    m_edit->setRange(minimum, maximum);
    m_edit->setSingleStep(step);
    m_reset = createResetButton(this);
    layout->addWidget(m_edit);
    layout->addWidget(m_reset);
    layout->addStretch(1);

    connect(m_reset, &QToolButton::clicked, this, [this]() {
        ConfigManager::instance()->removeValue(m_key);
    });
    connect(m_edit, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_guard)
            return;
        ConfigManager::instance()->setValue(m_key, value);
        applyState();
    });
    connect(ConfigManager::instance(), &ConfigManager::changed,
            this, &ConfigSpinBox::syncFromConfig);

    m_guard = true;
    m_edit->setValue(ConfigManager::instance()->intValue(m_key));
    m_guard = false;
    applyState();
}

void ConfigSpinBox::syncFromConfig(const QString& changedKey)
{
    if (changedKey != m_key)
        return;
    m_guard = true;
    m_edit->setValue(ConfigManager::instance()->intValue(m_key));
    m_guard = false;
    applyState();
}

void ConfigSpinBox::applyState()
{
    const bool modified = !ConfigManager::instance()->isDefault(m_key);
    m_reset->setVisible(modified);
    markModified(m_edit, modified);
}

ConfigDoubleSpinBox::ConfigDoubleSpinBox(const QString& key, double minimum, double maximum,
                                         double step, int decimals, QWidget* parent)
    : QWidget(parent)
    , m_key(key)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_edit = new QDoubleSpinBox(this);
    m_edit->setRange(minimum, maximum);
    m_edit->setSingleStep(step);
    m_edit->setDecimals(decimals);
    m_reset = createResetButton(this);
    layout->addWidget(m_edit);
    layout->addWidget(m_reset);
    layout->addStretch(1);

    connect(m_reset, &QToolButton::clicked, this, [this]() {
        ConfigManager::instance()->removeValue(m_key);
    });
    connect(m_edit, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_guard)
            return;
        ConfigManager::instance()->setValue(m_key, value);
        applyState();
    });
    connect(ConfigManager::instance(), &ConfigManager::changed,
            this, &ConfigDoubleSpinBox::syncFromConfig);

    m_guard = true;
    m_edit->setValue(ConfigManager::instance()->doubleValue(m_key));
    m_guard = false;
    applyState();
}

void ConfigDoubleSpinBox::syncFromConfig(const QString& changedKey)
{
    if (changedKey != m_key)
        return;
    m_guard = true;
    m_edit->setValue(ConfigManager::instance()->doubleValue(m_key));
    m_guard = false;
    applyState();
}

void ConfigDoubleSpinBox::applyState()
{
    const bool modified = !ConfigManager::instance()->isDefault(m_key);
    m_reset->setVisible(modified);
    markModified(m_edit, modified);
}

ConfigCheckBox::ConfigCheckBox(const QString& key, QWidget* parent)
    : QWidget(parent)
    , m_key(key)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_box = new QCheckBox(this);
    m_reset = createResetButton(this);
    layout->addWidget(m_box);
    layout->addWidget(m_reset);
    layout->addStretch(1);

    connect(m_reset, &QToolButton::clicked, this, [this]() {
        ConfigManager::instance()->removeValue(m_key);
    });
    connect(m_box, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_guard)
            return;
        ConfigManager::instance()->setValue(m_key, checked);
        applyState();
    });
    connect(ConfigManager::instance(), &ConfigManager::changed,
            this, &ConfigCheckBox::syncFromConfig);

    m_guard = true;
    m_box->setChecked(ConfigManager::instance()->boolValue(m_key));
    m_guard = false;
    applyState();
}

void ConfigCheckBox::syncFromConfig(const QString& changedKey)
{
    if (changedKey != m_key)
        return;
    m_guard = true;
    m_box->setChecked(ConfigManager::instance()->boolValue(m_key));
    m_guard = false;
    applyState();
}

void ConfigCheckBox::applyState()
{
    const bool modified = !ConfigManager::instance()->isDefault(m_key);
    m_reset->setVisible(modified);
}
