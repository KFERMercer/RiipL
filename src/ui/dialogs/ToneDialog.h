#pragma once

#include <QDialog>
#include <QList>
#include <QVector>

#include "core/translation/Tone.h"

class QPushButton;
class QStandardItem;
class QStandardItemModel;
class QTableView;
class QToolButton;

class ToneDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ToneDialog(const QVector<CustomTone>& customTones, QWidget* parent = nullptr);

    void reject() override;

    QVector<CustomTone> customTones() const;

private slots:
    void addTone();
    void removeTone();
    void moveTone(int offset);
    void refreshButtons();

private:
    bool isDirty() const;
    void loadTones(const QVector<CustomTone>& stored);
    int currentRow() const;
    QList<QStandardItem*> rowFor(const CustomTone& tone) const;

    QTableView* m_custom = nullptr;
    QStandardItemModel* m_customModel = nullptr;
    QPushButton* m_removeButton = nullptr;
    QToolButton* m_moveUpButton = nullptr;
    QToolButton* m_moveDownButton = nullptr;
    QVector<CustomTone> m_initialTones;
};
