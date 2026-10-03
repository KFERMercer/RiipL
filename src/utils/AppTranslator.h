#pragma once

#include <QLocale>
#include <QObject>
#include <QString>
#include <QTranslator>

// Owns the translators for the application catalog and the Qt built-in one; a
// QObject so a connection can be bound to its lifetime.
class AppTranslator : public QObject
{
    Q_OBJECT

public:
    explicit AppTranslator(const QString& applicationCatalogRoot = QStringLiteral(":/i18n"));

    // Returns false when the request changed nothing, so a caller can skip work
    // that would only re-broadcast a language change.
    bool apply(const QLocale& locale);

    QLocale locale() const { return m_locale; }

private:
    // Loads the catalog Qt picks for \p locale under \p directory, leaving the
    // source strings in place when none exists.
    static void load(QTranslator& translator,
                     const QLocale& locale,
                     const QString& base,
                     const QString& directory);

    QString m_catalogRoot;
    QTranslator m_app;
    QTranslator m_qt;
    QLocale m_locale;
    bool m_applied = false;
    bool m_guarded = false;
};
