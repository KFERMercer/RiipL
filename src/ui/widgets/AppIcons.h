#pragma once

#include <QIcon>

#include "core/translation/TranslationActivity.h"

namespace TrayIcons {

// Icon family the tray draws from; a start picks the easter-egg one at random.
enum class Family {
    App,
    Dude,
};

}

class AppIcons
{
public:
    static QIcon swapHorizontal();
    static QIcon moveUp();
    static QIcon moveDown();
    static QIcon arrowRight();
    static QIcon reset();
    static QIcon undo();
    static QIcon redo();
    static QIcon tray(TrayIcons::Family family, TranslationActivity::State state);
    static QIcon trayIdle(TrayIcons::Family family);
};
