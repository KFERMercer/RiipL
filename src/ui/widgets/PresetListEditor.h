#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "core/config/Preset.h"

class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QStandardItem;
class QStandardItemModel;
class QToolButton;
class QTreeView;

// Lists named presets with the actions that manage them, so a window adds only
// the columns and the actions its own kind of preset needs.
class PresetListEditor : public QWidget
{
    Q_OBJECT

public:
    // A captured field shown as a column after the name.
    struct FieldColumn
    {
        QString key;
        QString header;
    };

    struct Options
    {
        // Offers the search box filtering the listed presets.
        bool searchable = false;
        // Offers the button loading the highlighted preset.
        bool loadButton = false;
    };

    PresetListEditor(const QVector<FieldColumn>& fields, const Options& options,
                     QWidget* parent = nullptr);

    void setPresets(const QVector<Preset>& presets);
    QVector<Preset> presets() const;
    int selectedIndex() const;
    void setSelectedIndex(int index);
    QString selectedName() const;
    // Values of the selected preset, with every captured field filled in.
    QJsonObject selectedValues() const;

    // Writes \p values into the selected preset, which keeps its name.
    void overwriteSelected(const QJsonObject& values);
    // Adds a preset named \p name, replacing the entry that carries it already.
    void saveAs(const QString& name, const QJsonObject& values);
    // Removes the selected preset, asking for confirmation first.
    void removeSelected();

signals:
    // A double click or the load action asked for the highlighted preset.
    void loadRequested();
    // The list or the selection changed.
    void changed();

private:
    QStringList fieldKeys() const;
    QList<QStandardItem*> rowFor(const Preset& preset) const;
    QStringList names() const;
    QString uniqueName(const QString& base) const;
    void refreshButtons();
    void moveSelected(int offset);
    void renameSelected();
    void copySelected();

    QVector<FieldColumn> m_fields;
    // Set while the model is filled in bulk, so one change is reported per fill.
    bool m_bulkChange = false;
    QStandardItemModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    QTreeView* m_tree = nullptr;
    QLineEdit* m_search = nullptr;
    QToolButton* m_moveUpButton = nullptr;
    QToolButton* m_moveDownButton = nullptr;
    QPushButton* m_loadButton = nullptr;
    QPushButton* m_renameButton = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPushButton* m_removeButton = nullptr;
};
