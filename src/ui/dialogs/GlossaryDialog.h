#pragma once

#include <QDialog>
#include <QWidget>

#include "core/models/Glossary.h"

class QLineEdit;
class QTableWidget;

class GlossaryTable : public QWidget
{
    Q_OBJECT

public:
    explicit GlossaryTable(QWidget* parent = nullptr);

    void setEntries(const QVector<GlossaryEntry>& entries);
    QVector<GlossaryEntry> entries() const;

signals:
    void entriesChanged();

private slots:
    void addRow();
    void removeSelected();
    void moveRow(int offset);
    void importJson();
    void exportJson();

private:
    void applyFilter();

    QTableWidget* m_table = nullptr;
    QLineEdit* m_filter = nullptr;
    bool m_guard = false;
};

class GlossaryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GlossaryDialog(QWidget* parent = nullptr);

private:
    GlossaryTable* m_table = nullptr;
};
