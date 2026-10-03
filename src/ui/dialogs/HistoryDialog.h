#pragma once

#include <QDialog>

#include "core/history/HistoryManager.h"

class QLineEdit;
class QModelIndex;
class QSortFilterProxyModel;
class QStandardItemModel;
class QTreeView;

class HistoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HistoryDialog(HistoryManager* history, QWidget* parent = nullptr);

signals:
    void reuseRequested(const TranslationRecord& record);

private:
    void reload();
    void reuseItem(const QModelIndex& index);

    HistoryManager* m_history = nullptr;
    QTreeView* m_tree = nullptr;
    QStandardItemModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    QLineEdit* m_search = nullptr;
    QVector<TranslationRecord> m_records;
};
