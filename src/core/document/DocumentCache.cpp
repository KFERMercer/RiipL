#include "DocumentCache.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

const QString kAnswerSuffix = QStringLiteral(".document.cache.json");
const QString kFailureSuffix = QStringLiteral(".document.failure.json");

}

DocumentCache::DocumentCache(const QString& rootPath)
    : m_rootPath(rootPath.isEmpty() ? standardRootPath() : rootPath)
{
}

QString DocumentCache::standardRootPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

QString DocumentCache::pathFor(const QString& sample, const QString& suffix) const
{
    if (sample.isEmpty())
        return QString();
    return QDir(m_rootPath).filePath(sample + suffix);
}

// QSaveFile, so a write cut short leaves the previous answer rather than half a
// JSON file.
bool DocumentCache::write(const QString& path, const QString& text) const
{
    if (path.isEmpty())
        return false;
    if (!QDir().mkpath(m_rootPath))
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
    const QString path = pathFor(sample, kAnswerSuffix);
    if (path.isEmpty())
        return std::nullopt;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return QString::fromUtf8(file.readAll());
}

bool DocumentCache::storeAnswer(const QString& sample, const QString& answer) const
{
    return write(pathFor(sample, kAnswerSuffix), answer);
}

bool DocumentCache::storeFailure(const QString& sample, const QString& reply) const
{
    return write(pathFor(sample, kFailureSuffix), reply);
}

void DocumentCache::removeFailure(const QString& sample) const
{
    const QString path = pathFor(sample, kFailureSuffix);
    if (!path.isEmpty())
        QFile::remove(path);
}

// The only place the cache location is listed; translating addresses files by name.
void DocumentCache::clearAll(const QString& rootPath)
{
    QDir root(rootPath.isEmpty() ? standardRootPath() : rootPath);
    const QStringList names = root.entryList(
        {QStringLiteral("*") + kAnswerSuffix, QStringLiteral("*") + kFailureSuffix}, QDir::Files);
    for (const QString& name : names)
        root.remove(name);
}
