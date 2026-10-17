#pragma once

#include <QDialog>
#include <QVector>

#include "core/history/HistoryManager.h"
#include "core/translation/Language.h"

class QLineEdit;
class QModelIndex;
class QPushButton;
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
    void refreshButtons();
    // Row of the selected record in the source model, or -1.
    int currentRow() const;
    void reuseItem(const QModelIndex& index);
    void removeCurrent();

    HistoryManager* m_history = nullptr;
    QTreeView* m_tree = nullptr;
    QStandardItemModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    QLineEdit* m_search = nullptr;
    QPushButton* m_reuseButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QVector<TranslationRecord> m_records;
    QVector<CustomLanguage> m_customLanguages;
};
