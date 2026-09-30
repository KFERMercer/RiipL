#pragma once

#include <QFont>

namespace AppFonts {

// For text whose columns must line up, such as configuration entries and
// prompt templates.
QFont fixedWidth();

// Font of the text panes, one step above the interface font.
QFont editorFont();

}
