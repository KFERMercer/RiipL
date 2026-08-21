#pragma once

#include <QDialog>

#include "core/history/HistoryManager.h"

class QLineEdit;
class QTreeWidget;

class HistoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HistoryDialog(HistoryManager* history, QWidget* parent = nullptr);

signals:
    void reuseRequested(const TranslationRecord& record);

private:
    void reload();
    void applyFilter();

    HistoryManager* m_history = nullptr;
    QTreeWidget* m_tree = nullptr;
    QLineEdit* m_search = nullptr;
    QVector<TranslationRecord> m_records;
};
