#include "ToneDialog.h"

#include "core/translation/Tone.h"
#include "utils/GeometryUtils.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int kNameColumn = 0;
constexpr int kKeyColumn = 1;
}

ToneDialog::ToneDialog(const QJsonArray& customTones, const QString& uiLanguage, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Manage tones"));

    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(tr("Preset tones"), this));
    m_presets = new QTreeWidget(this);
    m_presets->setHeaderLabels({tr("Display name"), tr("Key")});
    m_presets->setRootIsDecorated(false);
    m_presets->header()->setSectionResizeMode(QHeaderView::Stretch);
    for (const ToneItem& item : Tones::presets()) {
        auto* presetItem = new QTreeWidgetItem(m_presets);
        presetItem->setText(kNameColumn, Tones::presetDisplayName(item.key, uiLanguage));
        presetItem->setText(kKeyColumn, item.key);
        presetItem->setFlags(Qt::NoItemFlags);
    }
    m_presets->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_presets->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    layout->addWidget(m_presets);

    layout->addWidget(new QLabel(tr("Custom tones"), this));
    m_custom = new QTableWidget(0, 2, this);
    m_custom->setHorizontalHeaderLabels({tr("Display name"), tr("Key")});
    m_custom->horizontalHeader()->setStretchLastSection(true);
    m_custom->verticalHeader()->setVisible(false);
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

    resize(GeometryUtils::dialogInitialSize(this));
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

void ToneDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (!m_columnsInitialized) {
        const int half = m_custom->viewport()->width() / 2;
        if (half > 0) {
            m_custom->setColumnWidth(kNameColumn, half);
            m_columnsInitialized = true;
        }
    }
}

void ToneDialog::addTone()
{
    const int row = m_custom->rowCount();
    m_custom->insertRow(row);
    m_custom->setItem(row, kNameColumn, new QTableWidgetItem());
    m_custom->setItem(row, kKeyColumn, new QTableWidgetItem());
    m_custom->editItem(m_custom->item(row, kNameColumn));
}

void ToneDialog::removeTone()
{
    const int row = m_custom->currentRow();
    if (row >= 0)
        m_custom->removeRow(row);
}
