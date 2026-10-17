#pragma once

#include <QDialog>
#include <QList>
#include <QVector>

#include "core/translation/Language.h"

class QPushButton;
class QStandardItem;
class QStandardItemModel;
class QTableView;
class QToolButton;

class LanguageDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LanguageDialog(const QVector<CustomLanguage>& customLanguages,
                            QWidget* parent = nullptr);

    void reject() override;

    QVector<CustomLanguage> customLanguages() const;

private slots:
    void addLanguage();
    void removeLanguage();
    void moveLanguage(int offset);
    void refreshButtons();

private:
    // True while the custom languages differ from the ones the window opened with.
    bool isDirty() const;
    void loadLanguages(const QVector<CustomLanguage>& stored);
    int currentRow() const;
    QList<QStandardItem*> rowFor(const CustomLanguage& language) const;

    QTableView* m_custom = nullptr;
    QStandardItemModel* m_customModel = nullptr;
    QPushButton* m_removeButton = nullptr;
    QToolButton* m_moveUpButton = nullptr;
    QToolButton* m_moveDownButton = nullptr;
    QVector<CustomLanguage> m_initialLanguages;
};
