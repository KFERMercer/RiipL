#pragma once

#include <QJsonValue>
#include <QList>
#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QToolButton;

// Base class for settings editor widgets following the form-dialog pattern:
// an editor loads the current config value once on construction, holds it in
// a plain Qt control, exposes it via value(), and reports user modifications
// through edited(). Nothing is written back automatically; dialogs collect
// values and commit them explicitly.
class ConfigEditor : public QWidget
{
    Q_OBJECT

public:
    explicit ConfigEditor(const QString& key, QWidget* parent = nullptr);

    QString key() const { return m_key; }
    virtual QJsonValue value() const = 0;

signals:
    void edited();

protected:
    // Writes v into the wrapped control. Implementations must not emit edited().
    virtual void setControlValue(const QJsonValue& v) = 0;

    void setupDisplay(QWidget* displayWidget, QToolButton* resetButton);
    void loadConfigValue();
    // Forwards wrapped-control change notifications; suppresses programmatic echoes.
    void handleControlChange();
    void refreshModifiedState();

private:
    QString m_key;
    QWidget* m_display = nullptr;
    QToolButton* m_reset = nullptr;
    bool m_guard = false;
};

class ConfigLineEdit : public ConfigEditor
{
    Q_OBJECT

public:
    explicit ConfigLineEdit(const QString& key, bool password = false, QWidget* parent = nullptr);

    QJsonValue value() const override;
    QLineEdit* edit() const { return m_edit; }

protected:
    void setControlValue(const QJsonValue& v) override;

private:
    QLineEdit* m_edit = nullptr;
};

class ConfigComboBox : public ConfigEditor
{
    Q_OBJECT

public:
    explicit ConfigComboBox(const QString& key, QWidget* parent = nullptr);

    QJsonValue value() const override;
    QComboBox* box() const { return m_box; }
    void setItems(const QList<QPair<QString, QString>>& items);

protected:
    void setControlValue(const QJsonValue& v) override;

private:
    QComboBox* m_box = nullptr;
};

class ConfigTextEdit : public ConfigEditor
{
    Q_OBJECT

public:
    explicit ConfigTextEdit(const QString& key, int rows = 4, QWidget* parent = nullptr);

    QJsonValue value() const override;
    QPlainTextEdit* edit() const { return m_edit; }

protected:
    void setControlValue(const QJsonValue& v) override;

private:
    QPlainTextEdit* m_edit = nullptr;
};

class ConfigSpinBox : public ConfigEditor
{
    Q_OBJECT

public:
    explicit ConfigSpinBox(const QString& key, int minimum, int maximum, int step,
                           QWidget* parent = nullptr);

    QJsonValue value() const override;
    QSpinBox* edit() const { return m_edit; }

protected:
    void setControlValue(const QJsonValue& v) override;

private:
    QSpinBox* m_edit = nullptr;
};

class ConfigDoubleSpinBox : public ConfigEditor
{
    Q_OBJECT

public:
    explicit ConfigDoubleSpinBox(const QString& key, double minimum, double maximum,
                                 double step, int decimals, QWidget* parent = nullptr);

    QJsonValue value() const override;
    QDoubleSpinBox* edit() const { return m_edit; }

protected:
    void setControlValue(const QJsonValue& v) override;

private:
    QDoubleSpinBox* m_edit = nullptr;
};

class ConfigCheckBox : public ConfigEditor
{
    Q_OBJECT

public:
    explicit ConfigCheckBox(const QString& key, QWidget* parent = nullptr);

    QJsonValue value() const override;
    QCheckBox* box() const { return m_box; }

protected:
    void setControlValue(const QJsonValue& v) override;

private:
    QCheckBox* m_box = nullptr;
};
