#include "AboutDialog.h"

#include "utils/GeometryUtils.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QVBoxLayout>

namespace {

constexpr char kProjectUrl[] = "https://github.com/KFERMercer/RiipL";

}

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About RiipL"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));

    auto* layout = new QVBoxLayout(this);

    auto* headerRow = new QHBoxLayout();
    auto* icon = new QLabel(this);
    icon->setPixmap(QIcon(QStringLiteral(":/icons/app.svg")).pixmap(48, 48));
    icon->setAlignment(Qt::AlignTop);
    headerRow->addWidget(icon);
    headerRow->addSpacing(12);

    auto* textLayout = new QVBoxLayout();
    auto* title = new QLabel(tr("<b>RiipL %1</b>").arg(QCoreApplication::applicationVersion()), this);
    auto* description = new QLabel(
        tr("An AI-powered desktop translator.<br/>Built with Qt %1.")
            .arg(QString::fromLatin1(qVersion())), this);
    auto* homepage = new QLabel(tr("Project homepage: <a href=\"%1\">%1</a>")
                                    .arg(QString::fromLatin1(kProjectUrl)), this);
    for (QLabel* label : { description, homepage }) {
        label->setTextFormat(Qt::RichText);
        label->setTextInteractionFlags(Qt::TextBrowserInteraction);
        label->setOpenExternalLinks(true);
        label->setWordWrap(true);
    }
    textLayout->addWidget(title);
    textLayout->addWidget(description);
    textLayout->addWidget(homepage);
    headerRow->addLayout(textLayout, 1);
    layout->addLayout(headerRow);

    layout->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);

    resize(GeometryUtils::dialogInitialSize(this));
}
