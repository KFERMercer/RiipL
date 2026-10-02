#include "DocumentCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

const QString kDocumentPrefix = QStringLiteral("document_");
const QString kCacheSuffix = QStringLiteral(".cache.json");
const QString kFailureSuffix = QStringLiteral(".failure.json");

}

DocumentCache::DocumentCache(const QString& checksum, const QString& rootPath)
    : m_checksum(checksum)
    , m_rootPath(rootPath.isEmpty() ? standardRootPath() : rootPath)
{
}

QString DocumentCache::standardRootPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

QString DocumentCache::documentDir() const
{
    return QDir(m_rootPath).filePath(kDocumentPrefix + m_checksum);
}

QString DocumentCache::documentPath() const
{
    return QDir(documentDir()).filePath(m_checksum);
}

QString DocumentCache::pathFor(const QString& shard, const QString& suffix) const
{
    return QDir(documentDir()).filePath(shard + suffix);
}

// QSaveFile, so a write cut short leaves the previous answer rather than half a
// JSON file.
bool DocumentCache::write(const QString& path, const QString& text) const
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QByteArray data = text.toUtf8();
    if (file.write(data) != data.size())
        return false;
    return file.commit();
}

bool DocumentCache::storeDocument(const QString& filePath) const
{
    if (!isValid())
        return false;
    const QString target = documentPath();
    // The checksum names the content, so a copy already there is this document.
    if (QFile::exists(target))
        return true;
    if (!QDir().mkpath(documentDir()))
        return false;
    return QFile::copy(filePath, target);
}

std::optional<QString> DocumentCache::cachedShard(const QString& shard) const
{
    if (!isValid() || shard.isEmpty())
        return std::nullopt;
    QFile file(pathFor(shard, kCacheSuffix));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return QString::fromUtf8(file.readAll());
}

bool DocumentCache::storeShard(const QString& shard, const QString& answer) const
{
    if (!isValid() || shard.isEmpty())
        return false;
    return write(pathFor(shard, kCacheSuffix), answer);
}

bool DocumentCache::storeFailure(const QString& shard, const QString& answer) const
{
    if (!isValid() || shard.isEmpty())
        return false;
    return write(pathFor(shard, kFailureSuffix), answer);
}

void DocumentCache::removeFailure(const QString& shard) const
{
    if (!isValid() || shard.isEmpty())
        return;
    QFile::remove(pathFor(shard, kFailureSuffix));
}

void DocumentCache::removeFailures() const
{
    if (!isValid())
        return;
    const QDir dir(documentDir());
    const QStringList names =
        dir.entryList({QStringLiteral("*") + kFailureSuffix}, QDir::Files);
    for (const QString& name : names)
        QFile::remove(dir.filePath(name));
}

QString DocumentCache::checksumOf(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();
    return QString::fromLatin1(hash.result().toHex());
}

// Every document cache goes, including those of documents since removed.
void DocumentCache::clearAll(const QString& rootPath)
{
    QDir root(rootPath.isEmpty() ? standardRootPath() : rootPath);
    const QStringList names = root.entryList({kDocumentPrefix + QStringLiteral("*")},
                                             QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& name : names)
        QDir(root.filePath(name)).removeRecursively();
}
