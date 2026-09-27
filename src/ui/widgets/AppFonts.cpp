#include "AppFonts.h"

#include <QFontDatabase>

QFont AppFonts::fixedWidth()
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
}
