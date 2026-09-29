#pragma once

#include <QDialog>

#include <QJsonArray>
#include <QString>
#include <QVector>

class QPushButton;
class QTableWidget;
class QToolButton;
class QTreeWidget;

// A user-defined tone: a key the prompts carry and the name the user typed.
struct CustomTone
{
    QString key;
    QString name;
};

class ToneDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ToneDialog(const QJsonArray& customTones, QWidget* parent = nullptr);

    QVector<CustomTone> customTones() const;
    static QJsonArray toJson(const QVector<CustomTone>& tones);

private slots:
    void addTone();
    void removeTone();
    void moveTone(int offset);

private:
    void loadTones(const QJsonArray& stored);
    void refreshButtons();

    QTreeWidget* m_presets = nullptr;
    QTableWidget* m_custom = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QToolButton* m_moveUpButton = nullptr;
    QToolButton* m_moveDownButton = nullptr;
};
