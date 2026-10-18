#include <QtTest>

#include "TestSupport.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/translation/DocumentTranslator.h"
#include "core/translation/TranslationActivity.h"
#include "core/translation/TranslationPool.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>

#include <utility>

namespace {

QJsonObject chatBody(const QString& prompt)
{
    return QJsonObject{
        {QStringLiteral("model"), QStringLiteral("test-model")},
        {QStringLiteral("messages"),
         QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), prompt}}}}
    };
}

QString stateName(TranslationActivity::State state)
{
    switch (state) {
    case TranslationActivity::State::Ready: return QStringLiteral("ready");
    case TranslationActivity::State::Busy: return QStringLiteral("busy");
    case TranslationActivity::State::Done: return QStringLiteral("done");
    case TranslationActivity::State::Failure: return QStringLiteral("failure");
    }
    return QString();
}

}

class TestTranslationActivity : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void startsReady();
    void followsThePoolFromBusyToDone();
    void holdsAFailureUntilNewWork();
    void markReadyClearsTheOutcome();

private:
    TestSupport::StubChatServer m_api{[](const QString&) {
        return QByteArrayLiteral(R"({"choices":[{"message":{"content":"ok"}}]})");
    }};
};

void TestTranslationActivity::init()
{
    m_api.reset();
    m_api.hold = true;
    TranslationPool::instance()->reset();
    QDir().mkpath(TestSupport::tempDir());
    ConfigManager::createInstance(TestSupport::tempDir());
    ConfigManager* config = ConfigManager::instance();
    config->setValue(Keys::apiBaseUrl, QStringLiteral("http://127.0.0.1:%1/v1").arg(m_api.port()));
    config->setValue(Keys::apiStream, false);
    config->setValue(Keys::apiMaxConcurrency, 1);
}

// Nothing has run when the mode is turned on, and a ready state reports nothing.
void TestTranslationActivity::startsReady()
{
    TranslationActivity activity;
    QStringList states;
    connect(&activity, &TranslationActivity::stateChanged, this,
            [&states](TranslationActivity::State state) { states.append(stateName(state)); });

    QCOMPARE(activity.state(), TranslationActivity::State::Ready);
    activity.markReady();
    QCOMPARE(activity.state(), TranslationActivity::State::Ready);
    QVERIFY(states.isEmpty());
}

// The pool carrying a request is Busy, and the queue running out is Done.
void TestTranslationActivity::followsThePoolFromBusyToDone()
{
    TranslationActivity activity;
    QStringList states;
    connect(&activity, &TranslationActivity::stateChanged, this,
            [&states](TranslationActivity::State state) { states.append(stateName(state)); });

    TranslationPool* pool = TranslationPool::instance();
    pool->enqueue(chatBody(QStringLiteral("doc")), DocumentPriority::window, {}, {}, {});
    QVERIFY(TestSupport::waitFor([&] { return activity.state() == TranslationActivity::State::Busy; }, 5000));
    QCOMPARE(activity.state(), TranslationActivity::State::Busy);
    QVERIFY(TestSupport::waitFor([&] { return m_api.requests == 1; }, 5000));
    QCOMPARE(m_api.requests, 1);

    m_api.releaseNext(false);
    QVERIFY(TestSupport::waitFor([&] { return activity.state() == TranslationActivity::State::Done; }, 5000));
    QCOMPARE(activity.state(), TranslationActivity::State::Done);
    QVERIFY(!pool->isBusy());
    QCOMPARE(states, QStringList({QStringLiteral("busy"), QStringLiteral("done")}));
}

// A failure outlives the queue that carried it: the pool emptying while a
// request beside the failed one is still in flight does not turn it into Done.
void TestTranslationActivity::holdsAFailureUntilNewWork()
{
    ConfigManager::instance()->setValue(Keys::apiMaxConcurrency, 2);
    TranslationActivity activity;
    QStringList states;
    connect(&activity, &TranslationActivity::stateChanged, this,
            [&states](TranslationActivity::State state) { states.append(stateName(state)); });

    TranslationPool* pool = TranslationPool::instance();
    pool->enqueue(chatBody(QStringLiteral("first")), DocumentPriority::window, {}, {}, {});
    pool->enqueue(chatBody(QStringLiteral("second")), DocumentPriority::window, {}, {}, {});
    QVERIFY(TestSupport::waitFor([&] { return activity.state() == TranslationActivity::State::Busy; }, 5000));
    QCOMPARE(activity.state(), TranslationActivity::State::Busy);
    QVERIFY(TestSupport::waitFor([&] { return m_api.requests == 2; }, 5000));
    QCOMPARE(m_api.requests, 2);

    // One failure leaves the pool carrying the request beside it.
    m_api.releaseNext(true);
    QVERIFY(TestSupport::waitFor([&] { return activity.state() == TranslationActivity::State::Failure; }, 5000));
    QCOMPARE(activity.state(), TranslationActivity::State::Failure);
    QVERIFY(pool->isBusy());

    // The queue runs out on the success beside the failure, which the state
    // holds.
    m_api.releaseNext(false);
    QVERIFY(TestSupport::waitFor([&] { return !pool->isBusy(); }, 5000));
    QTest::qWait(50);
    QCOMPARE(activity.state(), TranslationActivity::State::Failure);

    pool->enqueue(chatBody(QStringLiteral("later")), DocumentPriority::window, {}, {}, {});
    QVERIFY(TestSupport::waitFor([&] { return activity.state() == TranslationActivity::State::Busy; }, 5000));
    QCOMPARE(activity.state(), TranslationActivity::State::Busy);
    QVERIFY(TestSupport::waitFor([&] { return m_api.requests == 3; }, 5000));
    QCOMPARE(m_api.requests, 3);
    m_api.releaseAll();
    QVERIFY(TestSupport::waitFor([&] { return activity.state() == TranslationActivity::State::Done; }, 5000));
    QCOMPARE(activity.state(), TranslationActivity::State::Done);

    QCOMPARE(states, QStringList({QStringLiteral("busy"), QStringLiteral("failure"),
                                  QStringLiteral("busy"), QStringLiteral("done")}));
}

// A manual stop clears the outcome the last run left behind.
void TestTranslationActivity::markReadyClearsTheOutcome()
{
    TranslationActivity activity;
    TranslationPool* pool = TranslationPool::instance();

    pool->enqueue(chatBody(QStringLiteral("doc")), DocumentPriority::window, {}, {}, {});
    QVERIFY(TestSupport::waitFor([&] { return activity.state() == TranslationActivity::State::Busy; }, 5000));
    QCOMPARE(activity.state(), TranslationActivity::State::Busy);
    QVERIFY(TestSupport::waitFor([&] { return m_api.requests == 1; }, 5000));
    QCOMPARE(m_api.requests, 1);
    m_api.releaseNext(false);
    QVERIFY(TestSupport::waitFor([&] { return activity.state() == TranslationActivity::State::Done; }, 5000));
    QCOMPARE(activity.state(), TranslationActivity::State::Done);
    activity.markReady();
    QCOMPARE(activity.state(), TranslationActivity::State::Ready);

    pool->enqueue(chatBody(QStringLiteral("doc")), DocumentPriority::window, {}, {}, {});
    QVERIFY(TestSupport::waitFor([&] { return activity.state() == TranslationActivity::State::Busy; }, 5000));
    QCOMPARE(activity.state(), TranslationActivity::State::Busy);
    QVERIFY(TestSupport::waitFor([&] { return m_api.requests == 2; }, 5000));
    QCOMPARE(m_api.requests, 2);
    m_api.releaseNext(true);
    QVERIFY(TestSupport::waitFor([&] { return activity.state() == TranslationActivity::State::Failure; }, 5000));
    QCOMPARE(activity.state(), TranslationActivity::State::Failure);
    activity.markReady();
    QCOMPARE(activity.state(), TranslationActivity::State::Ready);

    QTest::qWait(50);
    QCOMPARE(activity.state(), TranslationActivity::State::Ready);
}

QTEST_MAIN(TestTranslationActivity)
#include "tst_translationactivity.moc"
