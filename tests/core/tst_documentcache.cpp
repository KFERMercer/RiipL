#include <QtTest>

#include "TestSupport.h"
#include "core/config/ConfigManager.h"
#include "core/config/Defaults.h"
#include "core/document/DocumentCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace {

QString writeFile(const QString& name, const QByteArray& content)
{
    QDir().mkpath(TestSupport::tempDir());
    const QString path = TestSupport::tempDir() + QLatin1Char('/') + name;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QString();
    file.write(content);
    return path;
}

QString readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

// Digest a request is cached under.
QString sampleOf(const QByteArray& request)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(request, QCryptographicHash::Sha256).toHex());
}

QStringList entriesWithSuffix(const QString& root, const QString& suffix)
{
    return QDir(root).entryList({QStringLiteral("*") + suffix}, QDir::Files, QDir::Name);
}

} // namespace

class TestDocumentCache : public QObject
{
    Q_OBJECT

private slots:
    void usesTheStandardCacheLocation();
    void roundTripsAnswersByName();
    void keepsAndClearsFailures();
    void removesOnlyTheNamedFailure();
    void storesNothingWithoutASample();
    void keepsAnswersFlatInTheCacheLocation();
    void clearsEveryDocumentCache();
    void cachesByDefault();
};

// Without a root of its own the cache lives in the standard cache directory, and
// a given root is used as it stands.
void TestDocumentCache::usesTheStandardCacheLocation()
{
    const QString standard = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QVERIFY(!standard.isEmpty());
    QCOMPARE(DocumentCache().rootPath(), standard);
    QCOMPARE(DocumentCache(TestSupport::tempDir()).rootPath(), TestSupport::tempDir());
}

// An answer lies directly below the cache location under the whole digest of the
// request that produced it.
void TestDocumentCache::roundTripsAnswersByName()
{
    const QDir root(TestSupport::tempDir());
    const DocumentCache cache(root.path());
    const QString sample = sampleOf(QByteArrayLiteral("window one"));
    QCOMPARE(sample.size(), 64);
    QVERIFY(!cache.cachedAnswer(sample));

    const QString answer = QStringLiteral("{\n    \"1\": \"译文\"\n}");
    QVERIFY(cache.storeAnswer(sample, answer));
    QVERIFY(cache.cachedAnswer(sample) == answer);
    QCOMPARE(root.entryList(QDir::Files, QDir::Name),
             QStringList{sample + QStringLiteral(".document.cache.json")});
}

void TestDocumentCache::keepsAndClearsFailures()
{
    const QDir root(TestSupport::tempDir());
    const DocumentCache cache(root.path());
    const QString sample = sampleOf(QByteArrayLiteral("window one"));
    const QString broken = QStringLiteral("{\"1\": \"译文\", \"2\":");
    const QString path = root.filePath(sample + QStringLiteral(".document.failure.json"));

    QVERIFY(cache.storeFailure(sample, broken));
    QCOMPARE(readFile(path), broken);

    cache.removeFailure(sample);
    QVERIFY(!QFile::exists(path));
}

// A request clears its own record and leaves the failure another request left.
void TestDocumentCache::removesOnlyTheNamedFailure()
{
    const DocumentCache cache(TestSupport::tempDir());
    const QString first = sampleOf(QByteArrayLiteral("window one"));
    const QString second = sampleOf(QByteArrayLiteral("window two"));
    QVERIFY(cache.storeAnswer(first, QStringLiteral("{}")));
    QVERIFY(cache.storeFailure(first, QStringLiteral("{")));
    QVERIFY(cache.storeFailure(second, QStringLiteral("{")));

    cache.removeFailure(first);

    QCOMPARE(entriesWithSuffix(TestSupport::tempDir(), QStringLiteral(".document.failure.json")),
             QStringList{second + QStringLiteral(".document.failure.json")});
    QVERIFY(cache.cachedAnswer(first) == QStringLiteral("{}"));
}

// A request without a name is cached nowhere: nothing is written and nothing is
// read.
void TestDocumentCache::storesNothingWithoutASample()
{
    const DocumentCache cache(TestSupport::tempDir());
    QVERIFY(!cache.storeAnswer(QString(), QStringLiteral("{}")));
    QVERIFY(!cache.storeFailure(QString(), QStringLiteral("{")));
    QVERIFY(!cache.cachedAnswer(QString()));
    cache.removeFailure(QString());
    QVERIFY(QDir(TestSupport::tempDir()).entryList(QDir::Files).isEmpty());
}

// Every answer sits in the cache location itself: a window is found by name, and
// any cache reads what another wrote.
void TestDocumentCache::keepsAnswersFlatInTheCacheLocation()
{
    const QDir root(TestSupport::tempDir());
    const DocumentCache first(root.path());
    const DocumentCache second(root.path());
    const QString one = sampleOf(QByteArrayLiteral("window one"));
    const QString two = sampleOf(QByteArrayLiteral("window two"));
    QVERIFY(first.storeAnswer(one, QStringLiteral("{}")));
    QVERIFY(second.storeAnswer(two, QStringLiteral("{}")));

    QCOMPARE(root.entryList(QDir::Files, QDir::Name),
             QStringList({one + QStringLiteral(".document.cache.json"),
                          two + QStringLiteral(".document.cache.json")}));
    QVERIFY(root.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    QVERIFY(second.cachedAnswer(one) == QStringLiteral("{}"));
}

// Clearing takes the answers and failures of every request and leaves the rest of
// the cache location alone.
void TestDocumentCache::clearsEveryDocumentCache()
{
    const DocumentCache cache(TestSupport::tempDir());
    const QString sample = sampleOf(QByteArrayLiteral("window one"));
    QVERIFY(cache.storeAnswer(sample, QStringLiteral("{}")));
    QVERIFY(cache.storeFailure(sample, QStringLiteral("{")));
    QVERIFY(cache.storeFailure(sampleOf(QByteArrayLiteral("window two")), QStringLiteral("{")));
    const QString unrelated =
        writeFile(QStringLiteral("unrelated.txt"), QByteArrayLiteral("keep me"));

    DocumentCache::clearAll(TestSupport::tempDir());

    QVERIFY(entriesWithSuffix(TestSupport::tempDir(), QStringLiteral(".document.cache.json")).isEmpty());
    QVERIFY(entriesWithSuffix(TestSupport::tempDir(), QStringLiteral(".document.failure.json"))
                .isEmpty());
    QVERIFY(QFile::exists(unrelated));
}

void TestDocumentCache::cachesByDefault()
{
    ConfigManager::createInstance(TestSupport::tempDir());
    QCOMPARE(Defaults::documentCacheEnabled, true);
    QVERIFY(ConfigManager::instance()->boolValue(Keys::documentCacheEnabled));
}

QTEST_MAIN(TestDocumentCache)
#include "tst_documentcache.moc"
