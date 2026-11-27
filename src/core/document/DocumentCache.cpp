#include "DocumentCache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

const QString kDocumentDir = QStringLiteral("document");
const QString kFailureDir = QStringLiteral("failure");
const QString kFileSuffix = QStringLiteral(".json");

}

DocumentCache::DocumentCache(const QString& rootPath)
    : m_rootPath(rootPath.isEmpty() ? standardRootPath() : rootPath)
{
}

QString DocumentCache::standardRootPath()
{
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cache.isEmpty())
        return QString();
    return QDir(cache).filePath(kDocumentDir);
}

QString DocumentCache::answerPath(const QString& sample) const
{
    if (sample.isEmpty() || m_rootPath.isEmpty())
        return QString();
    return QDir(m_rootPath).filePath(sample + kFileSuffix);
}

QString DocumentCache::failurePath(const QString& sample) const
{
    if (sample.isEmpty() || m_rootPath.isEmpty())
        return QString();
    return QDir(QDir(m_rootPath).filePath(kFailureDir)).filePath(sample + kFileSuffix);
}

// QSaveFile, so a write cut short leaves the previous answer rather than half a
// JSON file.
bool DocumentCache::write(const QString& path, const QString& text) const
{
    if (path.isEmpty())
        return false;
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

std::optional<QString> DocumentCache::cachedAnswer(const QString& sample) const
{
    const QString path = answerPath(sample);
    if (path.isEmpty())
        return std::nullopt;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return QString::fromUtf8(file.readAll());
}

bool DocumentCache::storeAnswer(const QString& sample, const QString& answer) const
{
    return write(answerPath(sample), answer);
}

bool DocumentCache::storeFailure(const QString& sample, const QString& reply) const
{
    return write(failurePath(sample), reply);
}

void DocumentCache::removeFailure(const QString& sample) const
{
    const QString path = failurePath(sample);
    if (!path.isEmpty())
        QFile::remove(path);
}

void DocumentCache::clearAll(const QString& rootPath)
{
    const QString root = rootPath.isEmpty() ? standardRootPath() : rootPath;
    if (!root.isEmpty())
        QDir(root).removeRecursively();
}
