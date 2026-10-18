#include <QtTest>

#include "TestSupport.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/document/DocumentCache.h"
#include "core/translation/DocumentTranslator.h"
#include "core/translation/TranslationPool.h"
#include "core/translation/YoloTranslator.h"

#include <QDir>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>

#include <memory>
#include <utility>

namespace {

// One JSON entry per window line, keyed from 1 on.
QByteArray translationOf(const QString& prompt)
{
    const QJsonObject window = TestSupport::documentWindowIn(prompt);
    QJsonObject answer;
    for (auto it = window.constBegin(); it != window.constEnd(); ++it)
        answer.insert(it.key(), QStringLiteral("译文 %1").arg(it.value().toString()));

    const QJsonObject choice{
        {QStringLiteral("message"),
         QJsonObject{{QStringLiteral("content"),
                      QString::fromUtf8(QJsonDocument(answer).toJson(QJsonDocument::Compact))}}},
        {QStringLiteral("finish_reason"), QStringLiteral("stop")}
    };
    return QJsonDocument(QJsonObject{{QStringLiteral("choices"), QJsonArray{choice}}})
        .toJson(QJsonDocument::Compact);
}

QJsonObject chatBody(const QString& prompt)
{
    return QJsonObject{
        {QStringLiteral("model"), QStringLiteral("test-model")},
        {QStringLiteral("messages"),
         QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), prompt}}}}
    };
}

}

class TestYoloTranslator : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void translatesThroughTheDocumentPath();
    void answersARepeatedTextFromTheDocumentCache();
    void takesTheConnectionAheadOfAQueuedDocument();
    void replacesTheRunInFlight();
    void retriesAWindowOnce();
    void givesUpAfterTheRetry();
    void stopsTheRunInFlight();

private:
    TestSupport::StubChatServer m_api{translationOf};
};

// A clipboard run caches its answers below the standard location, so the suite
// keeps them in Qt's test tree rather than the user's cache directory.
void TestYoloTranslator::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TestYoloTranslator::cleanupTestCase()
{
    DocumentCache::clearAll();
    QStandardPaths::setTestModeEnabled(false);
}

void TestYoloTranslator::init()
{
    m_api.reset();
    m_api.hold = false;
    TranslationPool::instance()->reset();
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    ConfigManager* config = ConfigManager::instance();
    config->setValue(Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(m_api.port()));
    config->setValue(Keys::apiStream, false);
    // Only the cache case wants a run answered without reaching the endpoint.
    config->setValue(Keys::documentCacheEnabled, false);
}

// A clipboard run goes through the document prompt and comes back as the whole
// text, window by window.
void TestYoloTranslator::translatesThroughTheDocumentPath()
{
    YoloTranslator translator;
    QString translated;
    int failed = 0;
    connect(&translator, &YoloTranslator::translated, this,
            [&translated](const QString& text) { translated = text; });
    connect(&translator, &YoloTranslator::failed, this, [&failed]() { ++failed; });

    TranslationContext context;
    context.targetLang = QStringLiteral("zh");
    translator.translate(QStringLiteral("alpha\nbeta\ngamma\n"), context);
    QVERIFY(translator.isRunning());
    QVERIFY(TestSupport::waitFor([&] { return !translated.isEmpty(); }, 10000));

    QCOMPARE(failed, 0);
    QVERIFY(!translator.isRunning());
    QCOMPARE(m_api.requests, 1);
    const QJsonObject window = TestSupport::documentWindowIn(m_api.prompts.first());
    QVERIFY(TestSupport::isNumberedObject(window));
    QCOMPARE(window.value(QStringLiteral("1")).toString(), QStringLiteral("alpha"));
    QCOMPARE(window.value(QStringLiteral("3")).toString(), QStringLiteral("gamma"));
    QVERIFY(m_api.prompts.first().contains(QStringLiteral("Every key is a line number")));
    QCOMPARE(translated, QStringLiteral("译文 alpha\n译文 beta\n译文 gamma\n"));
}

// A text the pipeline has already answered is served from the document cache, so
// a clipboard that repeats itself never reaches the endpoint again.
void TestYoloTranslator::answersARepeatedTextFromTheDocumentCache()
{
    DocumentCache::clearAll();
    ConfigManager::instance()->setValue(Keys::documentCacheEnabled, true);

    YoloTranslator translator;
    QStringList translated;
    connect(&translator, &YoloTranslator::translated, this,
            [&translated](const QString& text) { translated.append(text); });

    const TranslationContext context;
    translator.translate(QStringLiteral("alpha\nbeta\n"), context);
    QVERIFY(TestSupport::waitFor([&] { return translated.size() == 1; }, 10000));
    QCOMPARE(translated.size(), 1);
    QCOMPARE(m_api.requests, 1);

    translator.translate(QStringLiteral("alpha\nbeta\n"), context);
    QVERIFY(TestSupport::waitFor([&] { return translated.size() == 2; }, 10000));
    QCOMPARE(translated.size(), 2);

    QCOMPARE(m_api.requests, 1);
    QCOMPARE(translated.at(1), translated.at(0));
}

void TestYoloTranslator::takesTheConnectionAheadOfAQueuedDocument()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 1);
    m_api.hold = true;
    TranslationPool* pool = TranslationPool::instance();

    // Occupies the one connection while the YOLO run and the document request
    // wait behind it.
    pool->enqueue(chatBody(QStringLiteral("blocker")), DocumentPriority::window, {}, {}, {});
    QVERIFY(TestSupport::waitFor([&] { return m_api.requests == 1; }, 5000));
    QCOMPARE(m_api.requests, 1);
    pool->enqueue(chatBody(QStringLiteral("document")), DocumentPriority::window, {}, {}, {});

    YoloTranslator translator;
    QString translated;
    connect(&translator, &YoloTranslator::translated, this,
            [&translated](const QString& text) { translated = text; });
    translator.translate(QStringLiteral("clipboard line\n"), TranslationContext());

    m_api.releaseAll();
    QVERIFY(TestSupport::waitFor([&] { return m_api.requests == 3; }, 10000));
    QCOMPARE(m_api.requests, 3);
    QVERIFY(TestSupport::waitFor([&] { return !translated.isEmpty(); }, 10000));

    QCOMPARE(m_api.prompts.at(0), QStringLiteral("blocker"));
    QVERIFY(m_api.prompts.at(1).contains(QStringLiteral("clipboard line")));
    QCOMPARE(m_api.prompts.at(2), QStringLiteral("document"));
}

// A text handed in while a run is under way replaces it: the earlier request is
// dropped without a result and only the new text is translated.
void TestYoloTranslator::replacesTheRunInFlight()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 1);
    m_api.hold = true;

    YoloTranslator translator;
    QStringList translated;
    int failed = 0;
    connect(&translator, &YoloTranslator::translated, this,
            [&translated](const QString& text) { translated.append(text); });
    connect(&translator, &YoloTranslator::failed, this, [&failed]() { ++failed; });

    translator.translate(QStringLiteral("alpha\nbeta\n"), TranslationContext());
    QVERIFY(TestSupport::waitFor([&] { return m_api.requests == 1; }, 5000));
    QCOMPARE(m_api.requests, 1);

    translator.translate(QStringLiteral("gamma\ndelta\n"), TranslationContext());
    m_api.releaseAll();
    QVERIFY(TestSupport::waitFor([&] { return translated.size() == 1; }, 10000));
    QCOMPARE(translated.size(), 1);

    QCOMPARE(failed, 0);
    QVERIFY(!translator.isRunning());
    QCOMPARE(m_api.requests, 2);
    QCOMPARE(translated.first(), QStringLiteral("译文 gamma\n译文 delta\n"));
}

// The clipboard run gives a window one attempt after the first.
void TestYoloTranslator::retriesAWindowOnce()
{
    m_api.failuresLeft = 1;

    YoloTranslator translator;
    QString translated;
    int failed = 0;
    connect(&translator, &YoloTranslator::translated, this,
            [&translated](const QString& text) { translated = text; });
    connect(&translator, &YoloTranslator::failed, this, [&failed]() { ++failed; });

    translator.translate(QStringLiteral("alpha\nbeta\n"), TranslationContext());
    QVERIFY(TestSupport::waitFor([&] { return !translated.isEmpty(); }, 15000));

    QCOMPARE(failed, 0);
    QCOMPARE(m_api.requests, 2);
    QCOMPARE(m_api.prompts.at(1), m_api.prompts.at(0));
    QVERIFY(!translator.isRunning());
}

// A window that fails twice is reported as a failure without a third attempt.
void TestYoloTranslator::givesUpAfterTheRetry()
{
    m_api.failuresLeft = 10;

    YoloTranslator translator;
    QString translated;
    int failed = 0;
    connect(&translator, &YoloTranslator::translated, this,
            [&translated](const QString& text) { translated = text; });
    connect(&translator, &YoloTranslator::failed, this, [&failed]() { ++failed; });

    translator.translate(QStringLiteral("alpha\nbeta\n"), TranslationContext());
    QVERIFY(TestSupport::waitFor([&] { return failed == 1; }, 15000));
    QCOMPARE(failed, 1);

    QCOMPARE(m_api.requests, 2);
    QVERIFY(translated.isEmpty());
    QVERIFY(!translator.isRunning());
}

// A stop ends the run in flight without a translation.
void TestYoloTranslator::stopsTheRunInFlight()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 1);
    m_api.hold = true;

    YoloTranslator translator;
    QString translated;
    int stopped = 0;
    connect(&translator, &YoloTranslator::translated, this,
            [&translated](const QString& text) { translated = text; });
    connect(&translator, &YoloTranslator::stopped, this, [&stopped]() { ++stopped; });

    translator.translate(QStringLiteral("alpha\nbeta\n"), TranslationContext());
    QVERIFY(TestSupport::waitFor([&] { return m_api.requests == 1; }, 5000));
    QCOMPARE(m_api.requests, 1);
    QVERIFY(translator.isRunning());

    translator.stop();
    QVERIFY(TestSupport::waitFor([&] { return stopped == 1; }, 5000));
    QCOMPARE(stopped, 1);
    QVERIFY(!translator.isRunning());

    m_api.releaseAll();
    QTest::qWait(100);
    QVERIFY(translated.isEmpty());
    QCOMPARE(m_api.requests, 1);
}

QTEST_MAIN(TestYoloTranslator)
#include "tst_yolotranslator.moc"
