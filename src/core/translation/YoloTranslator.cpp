#include "YoloTranslator.h"

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/document/DocumentCache.h"
#include "core/document/DocumentSegmenter.h"

#include <QStringList>

#include <optional>
#include <utility>

namespace {

// Attempts a clipboard window gets after the first.
constexpr int kClipboardRetries = 1;

}

YoloTranslator::YoloTranslator(QObject* parent)
    : QObject(parent)
{
    connect(&m_translator, &DocumentTranslator::finished, this, &YoloTranslator::translated);
    connect(&m_translator, &DocumentTranslator::stopped, this, &YoloTranslator::stopped);
    // A clipboard run reports a failure without the windows it left out.
    connect(&m_translator, &DocumentTranslator::failed, this,
            [this](const QStringList&) { emit failed(); });
}

void YoloTranslator::translate(const QString& text, const TranslationContext& context)
{
    ConfigManager* config = ConfigManager::instance();
    const QVector<DocumentWindow> windows
        = DocumentSegmenter::partition(text, config->intValue(Keys::documentWindowWords),
                                       config->intValue(Keys::documentWindowLines));
    if (windows.isEmpty()) {
        stop();
        return;
    }

    DocumentRunOptions options;
    options.priority = YoloPriority::clipboard;
    options.priorityRetry = YoloPriority::clipboardRetry;
    options.retryCount = kClipboardRetries;

    // A clipboard run shares the document cache, so a text that has been through
    // the pipeline already is answered without reaching the endpoint.
    std::optional<DocumentCache> cache = std::nullopt;
    if (config->boolValue(Keys::documentCacheEnabled))
        cache.emplace();

    m_translator.start(windows, context, std::move(cache), options);
}

void YoloTranslator::stop()
{
    m_translator.stop();
}
