#pragma once

#include <QDialog>

#include <QJsonArray>
#include <QVector>

#include "core/translation/Tone.h"

class QTableWidget;
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

private:
    void loadTones(const QJsonArray& stored);

    QTreeWidget* m_presets = nullptr;
    QTableWidget* m_custom = nullptr;
};
