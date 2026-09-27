#pragma once

#include <QString>

class QSplitter;
class QWidget;

namespace WindowState {

namespace Id {
inline const QString main = QStringLiteral("main");
inline const QString about = QStringLiteral("about");
inline const QString apiPresets = QStringLiteral("api_presets");
inline const QString document = QStringLiteral("document");
inline const QString glossary = QStringLiteral("glossary");
inline const QString history = QStringLiteral("history");
inline const QString promptPreview = QStringLiteral("prompt_preview");
inline const QString settings = QStringLiteral("settings");
inline const QString tones = QStringLiteral("tones");
}

// Restores stored geometry and splitter state, then re-saves both on hide.
// Returns false when nothing was restored, so the caller can apply a default.
bool track(QWidget* window, const QString& id, QSplitter* splitter = nullptr);

QString filePath();

}
