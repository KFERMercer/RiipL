#pragma once

#include <QDialog>

#include <QJsonArray>
#include <QList>
#include <QString>
#include <QVector>

class QPushButton;
class QStandardItem;
class QStandardItemModel;
class QTableView;
class QToolButton;
class QTreeView;

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

    void reject() override;

    QVector<CustomTone> customTones() const;
    static QJsonArray toJson(const QVector<CustomTone>& tones);

private slots:
    void addTone();
    void removeTone();
    void moveTone(int offset);
    void refreshButtons();

private:
    // True while the custom tones differ from the ones the window opened with.
    bool isDirty() const;
    void loadTones(const QJsonArray& stored);
    int currentRow() const;
    QList<QStandardItem*> rowFor(const CustomTone& tone) const;

    QTreeView* m_presets = nullptr;
    QStandardItemModel* m_presetModel = nullptr;
    QTableView* m_custom = nullptr;
    QStandardItemModel* m_customModel = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QToolButton* m_moveUpButton = nullptr;
    QToolButton* m_moveDownButton = nullptr;
    QJsonArray m_initialTones;
};
