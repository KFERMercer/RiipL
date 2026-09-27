#pragma once

#include <QDialog>
#include <QWidget>

#include "core/models/Glossary.h"

class QLineEdit;
class QPushButton;
class QTableWidget;
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
    void applyFilter();

    QTableWidget* m_table = nullptr;
    QLineEdit* m_filter = nullptr;
    QPushButton* m_removeButton = nullptr;
    QToolButton* m_moveUpButton = nullptr;
    QToolButton* m_moveDownButton = nullptr;
};

class GlossaryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GlossaryDialog(QWidget* parent = nullptr);

private:
    GlossaryTable* m_table = nullptr;
};
