#include <QtTest>

#include <QDir>
#include <QEvent>
#include <QLocale>
#include <QTranslator>

#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "utils/AppTranslator.h"

// Counts the language changes Qt broadcasts when translators are swapped.
class LanguageChangeSpy : public QObject
{
public:
    int count = 0;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::LanguageChange)
            ++count;
        return QObject::eventFilter(watched, event);
    }
};

class TestAppTranslator : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void appliesOnlyWhenTheLanguageChanges();
    void appliesTheFirstRequestRegardlessOfTheSession();
    void toleratesAHandlerThatWritesTheLanguage();
    void selectsCatalogsByLanguageAndScript();

private:
    bool m_catalogAvailable = false;
};

namespace {

// The application embeds its catalogs, which a unit test does not link.
QString catalogRoot()
{
#ifdef RIIPL_CATALOG_DIR
    return QStringLiteral(RIIPL_CATALOG_DIR);
#else
    return QStringLiteral(":/i18n");
#endif
}

}

void TestAppTranslator::init()
{
    qApp->installEventFilter(this);
    // The catalog must be readable for the assertions to mean anything.
    if (!m_catalogAvailable) {
        QTranslator probe;
        const QString path = QDir(catalogRoot()).filePath(QStringLiteral("riipl_zh.qm"));
        m_catalogAvailable = probe.load(path);
        QVERIFY2(m_catalogAvailable, qPrintable(QStringLiteral("catalog not found: ") + path));
    }
}

void TestAppTranslator::appliesOnlyWhenTheLanguageChanges()
{
    AppTranslator translator(catalogRoot());

    // English is the source language: applying it installs nothing.
    QVERIFY(translator.apply(QLocale(QStringLiteral("en"))));
    QCOMPARE(QLocale::languageToCode(translator.locale().language()), QStringLiteral("en"));

    LanguageChangeSpy spy;
    qApp->installEventFilter(&spy);

    // The same request again is a no-op: no catalog is re-parsed.
    QVERIFY(!translator.apply(QLocale(QStringLiteral("en"))));
    QCOMPARE(spy.count, 0);

    if (m_catalogAvailable) {
        QVERIFY(translator.apply(QLocale(QStringLiteral("zh"))));
        QVERIFY(spy.count > 0);
        const int afterSwitch = spy.count;
        QVERIFY(!translator.apply(QLocale(QStringLiteral("zh"))));
        QCOMPARE(spy.count, afterSwitch);
    }

    qApp->removeEventFilter(&spy);
}

void TestAppTranslator::appliesTheFirstRequestRegardlessOfTheSession()
{
    // A default-constructed QLocale already equals the session locale, so the
    // first request must install its catalogs even when it matches the session.
    const QLocale session = QLocale::system();
    if (session.language() != QLocale::Chinese)
        QSKIP("needs a Chinese session locale to reproduce the collision");

    AppTranslator translator(catalogRoot());
    QVERIFY(translator.apply(session));
    QCOMPARE(QCoreApplication::translate("MainWindow", "RiipL Translator"),
             QStringLiteral("RiipL 翻译"));
}

void TestAppTranslator::toleratesAHandlerThatWritesTheLanguage()
{
    // A handler that writes the interface language from inside LanguageChange
    // must not re-enter apply() and corrupt the translator set.
    AppTranslator translator(catalogRoot());
    LanguageChangeSpy spy;
    qApp->installEventFilter(&spy);

    QObject handler;
    QObject::connect(ConfigManager::instance(), &ConfigManager::changed, &handler,
                     [](const QString& key) {
                         if (key == Keys::uiLanguage)
                             ConfigManager::instance()->setValue(Keys::uiLanguage, QStringLiteral("en"));
                     });

    QVERIFY(translator.apply(QLocale(QStringLiteral("zh"))));
    QCOMPARE(QLocale::languageToCode(translator.locale().language()), QStringLiteral("zh"));
    QVERIFY(Keys::uiLanguageCodes().contains(QLocale::languageToCode(translator.locale().language())));
    QVERIFY(translator.apply(QLocale(QStringLiteral("en"))));

    qApp->removeEventFilter(&spy);
}

void TestAppTranslator::selectsCatalogsByLanguageAndScript()
{
    AppTranslator translator(catalogRoot());

    // Qt resolves the catalog for a locale, so the interface language only has to
    // be handed over as one.
    QVERIFY(translator.apply(QLocale(QStringLiteral("zh"))));
    QCOMPARE(QCoreApplication::translate("SettingsDialog", "Settings"), QStringLiteral("设置"));

    // A language the application does not ship loads nothing, so the source
    // strings stay in place rather than being replaced by a wrong catalog.
    QVERIFY(translator.apply(QLocale(QStringLiteral("ja"))));
    QCOMPARE(QCoreApplication::translate("SettingsDialog", "Settings"), QStringLiteral("Settings"));

    translator.apply(QLocale(QStringLiteral("en")));
    QCOMPARE(QCoreApplication::translate("SettingsDialog", "Settings"), QStringLiteral("Settings"));
}

QTEST_MAIN(TestAppTranslator)
#include "tst_apptranslator.moc"
