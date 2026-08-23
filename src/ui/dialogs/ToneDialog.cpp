#include "ToneDialog.h"

#include "core/translation/Tone.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr int kKeyColumn = 0;
constexpr int kNameColumn = 1;
}

ToneDialog::ToneDialog(const QJsonArray& customTones, const QString& uiLanguage, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Manage tones"));
    resize(520, 460);

    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(tr("Preset tones"), this));
    m_presets = new QListWidget(this);
    for (const ToneItem& item : Tones::presets()) {
        const QString name = Tones::presetDisplayName(item.key, uiLanguage);
        auto* presetItem = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(name, item.key), m_presets);
        presetItem->setFlags(Qt::NoItemFlags);
    }
    m_presets->setFixedHeight(140);
    layout->addWidget(m_presets);

    layout->addWidget(new QLabel(tr("Custom tones"), this));
    m_custom = new QTableWidget(0, 2, this);
    m_custom->setHorizontalHeaderLabels({tr("Key"), tr("Display name")});
    m_custom->horizontalHeader()->setStretchLastSection(true);
    m_custom->verticalHeader()->setVisible(false);
    m_custom->setColumnWidth(kKeyColumn, 180);
    layout->addWidget(m_custom, 1);

    loadTones(customTones);

    auto* buttonRow = new QHBoxLayout();
    auto addButton = new QPushButton(tr("Add"), this);
    auto removeButton = new QPushButton(tr("Remove"), this);
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(removeButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(addButton, &QPushButton::clicked, this, &ToneDialog::addTone);
    connect(removeButton, &QPushButton::clicked, this, &ToneDialog::removeTone);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ToneDialog::loadTones(const QJsonArray& stored)
{
    m_custom->setRowCount(stored.size());
    for (int i = 0; i < stored.size(); ++i) {
        const QJsonObject object = stored.at(i).toObject();
        m_custom->setItem(i, kKeyColumn, new QTableWidgetItem(object.value(QStringLiteral("key")).toString()));
        m_custom->setItem(i, kNameColumn, new QTableWidgetItem(object.value(QStringLiteral("name")).toString()));
    }
}

QJsonArray ToneDialog::toJson(const QVector<ToneItem>& tones)
{
    QJsonArray array;
    for (const ToneItem& tone : tones) {
        QJsonObject object;
        object.insert(QStringLiteral("key"), tone.key);
        object.insert(QStringLiteral("name"), tone.en);
        array.append(object);
    }
    return array;
}

QVector<ToneItem> ToneDialog::customTones() const
{
    QVector<ToneItem> result;
    for (int i = 0; i < m_custom->rowCount(); ++i) {
        ToneItem tone;
        tone.key = m_custom->item(i, kKeyColumn) ? m_custom->item(i, kKeyColumn)->text().trimmed() : QString();
        tone.en = m_custom->item(i, kNameColumn) ? m_custom->item(i, kNameColumn)->text().trimmed() : QString();
        if (tone.key.isEmpty())
            continue;
        if (tone.en.isEmpty())
            tone.en = tone.key;
        tone.zh = tone.en;
        result.append(tone);
    }
    return result;
}

void ToneDialog::addTone()
{
    const int row = m_custom->rowCount();
    m_custom->insertRow(row);
    m_custom->setItem(row, kKeyColumn, new QTableWidgetItem());
    m_custom->setItem(row, kNameColumn, new QTableWidgetItem());
    m_custom->editItem(m_custom->item(row, kKeyColumn));
}

void ToneDialog::removeTone()
{
    const int row = m_custom->currentRow();
    if (row >= 0)
        m_custom->removeRow(row);
}
