#include "AppTranslator.h"

#include <QCoreApplication>
#include <QLibraryInfo>

AppTranslator::AppTranslator(const QString& applicationCatalogRoot)
    : m_catalogRoot(applicationCatalogRoot)
{
}

bool AppTranslator::apply(const QLocale& locale)
{
    // Removing or installing a translator makes Qt deliver LanguageChange, and a
    // handler that writes the interface language would re-enter here from inside
    // that delivery.
    if (m_guarded)
        return false;
    if (m_applied && locale == m_locale)
        return false;
    m_guarded = true;

    for (QTranslator* translator : {&m_app, &m_qt})
        QCoreApplication::removeTranslator(translator);

    m_locale = locale;
    m_applied = true;
    load(m_app, locale, QStringLiteral("riipl"), m_catalogRoot);
    load(m_qt, locale, QStringLiteral("qtbase"),
         QLibraryInfo::path(QLibraryInfo::TranslationsPath));

    m_guarded = false;
    return true;
}

// Qt resolves the locale itself, trying the full name and then the language
// alone; a missing catalog leaves the source strings in place.
void AppTranslator::load(QTranslator& translator,
                         const QLocale& locale,
                         const QString& base,
                         const QString& directory)
{
    if (translator.load(locale, base, QStringLiteral("_"), directory))
        QCoreApplication::installTranslator(&translator);
}
