#include "MessageBoxes.h"

#include <QCoreApplication>
#include <QMessageBox>

namespace {

struct MessageBoxesTr
{
    Q_DECLARE_TR_FUNCTIONS(MessageBoxes)
};

}

namespace MessageBoxes {

bool confirmDiscard(QWidget* parent)
{
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(MessageBoxesTr::tr("Unsaved changes"));
    box.setText(MessageBoxesTr::tr("Your changes have not been applied yet."));
    box.setStandardButtons(QMessageBox::Discard | QMessageBox::Cancel);
    // Cancel is the default, so a stray Return never drops pending edits.
    box.setDefaultButton(QMessageBox::Cancel);
    return box.exec() == QMessageBox::Discard;
}

}
