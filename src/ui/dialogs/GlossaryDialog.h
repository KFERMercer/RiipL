#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QList>
#include <QVector>
#include <QWidget>

#include "core/models/Glossary.h"

class QLineEdit;
class QPushButton;
class QStandardItem;
class QSortFilterProxyModel;
class QStandardItemModel;
class QTableView;
class QToolButton;

class GlossaryTable : public QWidget
{
    Q_OBJECT

public:
    explicit GlossaryTable(QWidget* parent = nullptr);

    void setEntries(const QVector<GlossaryEntry>& entries);
    QVector<GlossaryEntry> entries() const;

private slots:
    void addRow();
    void removeSelected();
    void moveRow(int offset);
    void importJson();
    void exportJson();
    void refreshButtons();

private:
    QList<QStandardItem*> rowFor(const GlossaryEntry& entry) const;

    QTableView* m_table = nullptr;
    QStandardItemModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    QLineEdit* m_filter = nullptr;
    QToolButton* m_moveUpButton = nullptr;
    QToolButton* m_moveDownButton = nullptr;
    QPushButton* m_removeButton = nullptr;
};

class GlossaryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GlossaryDialog(QWidget* parent = nullptr);

    void reject() override;

private:
    // True while the terms differ from the ones the window opened with.
    bool isDirty() const;

    GlossaryTable* m_table = nullptr;
    QJsonArray m_initialEntries;
};
