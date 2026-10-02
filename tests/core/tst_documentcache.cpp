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
    const QString path = TestSupport::tempDir() + QLatin1Char('/') + name;
    QDir().mkpath(TestSupport::tempDir());
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

} // namespace

class TestDocumentCache : public QObject
{
    Q_OBJECT

private slots:
    void checksumsFileContents();
    void usesTheStandardCacheLocation();
    void storesTheDocumentFile();
    void roundTripsShardAnswers();
    void keepsAndClearsFailures();
    void clearsEveryFailureOfADocument();
    void storesNothingWithoutChecksum();
    void clearsEveryDocumentCache();
    void cachesByDefault();
};

void TestDocumentCache::checksumsFileContents()
{
    const QByteArray content = QByteArrayLiteral("alpha\nbeta\n");
    const QString path = writeFile(QStringLiteral("document.txt"), content);
    QVERIFY(!path.isEmpty());

    const QString checksum =
        QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex());
    QCOMPARE(DocumentCache::checksumOf(path), checksum);

    // Other contents and unreadable files are named apart from it.
    const QString other = writeFile(QStringLiteral("other.txt"), QByteArrayLiteral("alpha\n"));
    QVERIFY(DocumentCache::checksumOf(other) != checksum);
    QVERIFY(DocumentCache::checksumOf(TestSupport::tempDir() + QStringLiteral("/missing.txt")).isEmpty());
}

// Without a root of its own the cache lives in the standard cache directory.
void TestDocumentCache::usesTheStandardCacheLocation()
{
    const DocumentCache cache(QStringLiteral("abc123"));
    const QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QVERIFY(!root.isEmpty());
    QCOMPARE(cache.documentDir(), QDir(root).filePath(QStringLiteral("document_abc123")));
    QCOMPARE(cache.documentPath(), QDir(cache.documentDir()).filePath(QStringLiteral("abc123")));
}

void TestDocumentCache::storesTheDocumentFile()
{
    const QByteArray content = QByteArrayLiteral("alpha\nbeta\n");
    const QString source = writeFile(QStringLiteral("document.txt"), content);
    const DocumentCache cache(DocumentCache::checksumOf(source), TestSupport::tempDir());

    QVERIFY(cache.storeDocument(source));
    QCOMPARE(readFile(cache.documentPath()), QString::fromUtf8(content));

    // A copy already there is the document, so it stands even when the file it
    // came from is gone.
    QVERIFY(cache.storeDocument(TestSupport::tempDir() + QStringLiteral("/missing.txt")));
    QCOMPARE(readFile(cache.documentPath()), QString::fromUtf8(content));
}

void TestDocumentCache::roundTripsShardAnswers()
{
    const DocumentCache cache(QStringLiteral("abc123"), TestSupport::tempDir());
    const QString shard = QStringLiteral("0.3f9a1c22");
    QVERIFY(!cache.cachedShard(shard));

    const QString answer = QStringLiteral("{\n    \"1\": \"译文\"\n}");
    QVERIFY(cache.storeShard(shard, answer));
    QVERIFY(cache.cachedShard(shard) == answer);
    QVERIFY(QFile::exists(cache.documentDir() + QStringLiteral("/0.3f9a1c22.cache.json")));

    // A shard without a name has no file to live in.
    QVERIFY(!cache.storeShard(QString(), answer));
    QVERIFY(!cache.cachedShard(QString()));
}

void TestDocumentCache::keepsAndClearsFailures()
{
    const DocumentCache cache(QStringLiteral("abc123"), TestSupport::tempDir());
    const QString shard = QStringLiteral("1.0011aabb");
    const QString broken = QStringLiteral("{\"1\": \"译文\", \"2\":");

    QVERIFY(cache.storeFailure(shard, broken));
    const QString path = cache.documentDir() + QStringLiteral("/1.0011aabb.failure.json");
    QCOMPARE(readFile(path), broken);

    cache.removeFailure(shard);
    QVERIFY(!QFile::exists(path));
}

// A run drops the failures of the previous one, leaving the answers alone.
void TestDocumentCache::clearsEveryFailureOfADocument()
{
    const DocumentCache cache(QStringLiteral("abc123"), TestSupport::tempDir());
    const QString shard = QStringLiteral("1.0011aabb");
    QVERIFY(cache.storeShard(shard, QStringLiteral("{}")));
    QVERIFY(cache.storeFailure(shard, QStringLiteral("{")));
    QVERIFY(cache.storeFailure(QStringLiteral("deadbeef.0"), QStringLiteral("{")));

    cache.removeFailures();

    QVERIFY(QDir(cache.documentDir())
                .entryList({QStringLiteral("*.failure.json")}, QDir::Files)
                .isEmpty());
    QVERIFY(cache.cachedShard(shard) == QStringLiteral("{}"));
}

// A cache without a checksum is a disabled cache: it reads and writes nothing.
void TestDocumentCache::storesNothingWithoutChecksum()
{
    const DocumentCache cache(QString(), TestSupport::tempDir());
    QVERIFY(!cache.isValid());

    const QString source = writeFile(QStringLiteral("document.txt"), QByteArrayLiteral("alpha"));
    QVERIFY(!cache.storeDocument(source));
    QVERIFY(!cache.storeShard(QStringLiteral("0.3f9a1c22"), QStringLiteral("{}")));
    QVERIFY(!cache.storeFailure(QStringLiteral("0.3f9a1c22"), QStringLiteral("{")));
    QVERIFY(!cache.cachedShard(QStringLiteral("0.3f9a1c22")));
    QVERIFY(!QDir(TestSupport::tempDir()).exists(QStringLiteral("document_")));
}

// Clearing takes the caches of every document and leaves the rest of the cache
// directory alone.
void TestDocumentCache::clearsEveryDocumentCache()
{
    const DocumentCache first(QStringLiteral("aaa"), TestSupport::tempDir());
    const DocumentCache second(QStringLiteral("bbb"), TestSupport::tempDir());
    QVERIFY(first.storeShard(QStringLiteral("0.3f9a1c22"), QStringLiteral("{}")));
    QVERIFY(second.storeShard(QStringLiteral("0.3f9a1c22"), QStringLiteral("{}")));
    QVERIFY(first.storeDocument(writeFile(QStringLiteral("document.txt"), QByteArrayLiteral("alpha"))));
    const QString unrelated = writeFile(QStringLiteral("unrelated.txt"), QByteArrayLiteral("keep me"));

    DocumentCache::clearAll(TestSupport::tempDir());

    QVERIFY(!QFile::exists(first.documentDir()));
    QVERIFY(!QFile::exists(second.documentDir()));
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
