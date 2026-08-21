#pragma once

#include <QDialog>

#include <QVector>

#include "core/translation/Tone.h"

class QListWidget;
class QLineEdit;
class QTableWidget;

class ToneDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ToneDialog(QWidget* parent = nullptr);

private slots:
    void addTone();
    void removeTone();

private:
    QVector<ToneItem> customTones() const;

    QListWidget* m_presets = nullptr;
    QTableWidget* m_custom = nullptr;
};
