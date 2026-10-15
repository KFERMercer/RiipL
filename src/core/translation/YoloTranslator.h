#pragma once

#include <QObject>
#include <QString>

#include "core/translation/DocumentTranslator.h"
#include "core/translation/PromptBuilder.h"

namespace YoloPriority {

inline constexpr int clipboard = 20;
inline constexpr int clipboardRetry = 19;

}

// Translates clipboard text along the document path, so a clipboard run shares the
// document prompt and the pool's concurrency. One instance holds one run: a text
// handed in while a run is under way cancels that run and takes its place.
class YoloTranslator : public QObject
{
    Q_OBJECT

public:
    explicit YoloTranslator(QObject* parent = nullptr);

    void translate(const QString& text, const TranslationContext& context);
    void stop();
    bool isRunning() const { return m_translator.isRunning(); }

signals:
    void translated(const QString& text);
    void failed();
    void stopped();

private:
    DocumentTranslator m_translator;
};
