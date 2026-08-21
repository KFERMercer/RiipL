#pragma once

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

class ConfigLineEdit : public QWidget
{
    Q_OBJECT

public:
    explicit ConfigLineEdit(const QString& key, bool password = false, QWidget* parent = nullptr);
    QLineEdit* edit() const { return m_edit; }

private slots:
    void syncFromConfig(const QString& changedKey);

private:
    void applyState();

    QString m_key;
    QLineEdit* m_edit = nullptr;
    QToolButton* m_reset = nullptr;
    bool m_guard = false;
};

class ConfigComboBox : public QWidget
{
    Q_OBJECT

public:
    explicit ConfigComboBox(const QString& key, QWidget* parent = nullptr);
    QComboBox* box() const { return m_box; }
    void setItems(const QList<QPair<QString, QString>>& items);

public slots:
    void reload();

private slots:
    void onConfigChanged(const QString& key);

private:
    void applyState();

    QString m_key;
    QComboBox* m_box = nullptr;
    QToolButton* m_reset = nullptr;
    bool m_guard = false;
};

class ConfigTextEdit : public QWidget
{
    Q_OBJECT

public:
    explicit ConfigTextEdit(const QString& key, int rows = 4, QWidget* parent = nullptr);
    QPlainTextEdit* edit() const { return m_edit; }

private slots:
    void syncFromConfig(const QString& changedKey);

private:
    void applyState();

    QString m_key;
    QPlainTextEdit* m_edit = nullptr;
    QToolButton* m_reset = nullptr;
    bool m_guard = false;
};

class ConfigSpinBox : public QWidget
{
    Q_OBJECT

public:
    explicit ConfigSpinBox(const QString& key, int minimum, int maximum, int step,
                           QWidget* parent = nullptr);
    QSpinBox* edit() const { return m_edit; }

private slots:
    void syncFromConfig(const QString& changedKey);

private:
    void applyState();

    QString m_key;
    QSpinBox* m_edit = nullptr;
    QToolButton* m_reset = nullptr;
    bool m_guard = false;
};

class ConfigDoubleSpinBox : public QWidget
{
    Q_OBJECT

public:
    explicit ConfigDoubleSpinBox(const QString& key, double minimum, double maximum,
                                 double step, int decimals, QWidget* parent = nullptr);
    QDoubleSpinBox* edit() const { return m_edit; }

private slots:
    void syncFromConfig(const QString& changedKey);

private:
    void applyState();

    QString m_key;
    QDoubleSpinBox* m_edit = nullptr;
    QToolButton* m_reset = nullptr;
    bool m_guard = false;
};

class ConfigCheckBox : public QWidget
{
    Q_OBJECT

public:
    explicit ConfigCheckBox(const QString& key, QWidget* parent = nullptr);
    QCheckBox* box() const { return m_box; }

private slots:
    void syncFromConfig(const QString& changedKey);

private:
    void applyState();

    QString m_key;
    QCheckBox* m_box = nullptr;
    QToolButton* m_reset = nullptr;
    bool m_guard = false;
};
