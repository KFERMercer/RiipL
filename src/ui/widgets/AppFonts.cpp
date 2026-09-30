#include "AppFonts.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"

#include <QApplication>
#include <QFontDatabase>

QFont AppFonts::fixedWidth()
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
}

QFont AppFonts::editorFont()
{
    QFont font = QApplication::font();
    font.setPointSize(ConfigManager::instance()->intValue(Keys::uiFontSize) + 1);
    return font;
}
