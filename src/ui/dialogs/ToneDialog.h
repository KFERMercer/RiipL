#pragma once

#include <QDialog>

#include <QJsonArray>
#include <QVector>

#include "core/translation/Tone.h"

class QPushButton;
class QTableWidget;
class QToolButton;
class QTreeWidget;

class ToneDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ToneDialog(const QJsonArray& customTones, const QString& uiLanguage,
                        QWidget* parent = nullptr);

    QVector<ToneItem> customTones() const;
    static QJsonArray toJson(const QVector<ToneItem>& tones);

private slots:
    void addTone();
    void removeTone();
    void moveTone(int offset);

private:
    void loadTones(const QJsonArray& stored);
    void refreshButtons();

    QTreeWidget* m_presets = nullptr;
    QTableWidget* m_custom = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QToolButton* m_moveUpButton = nullptr;
    QToolButton* m_moveDownButton = nullptr;
};
