#pragma once

class QWidget;

namespace MessageBoxes {

// True when the pending changes may be discarded.
bool confirmDiscard(QWidget* parent);

}
