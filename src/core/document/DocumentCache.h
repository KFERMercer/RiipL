#pragma once

#include <QString>

#include <optional>

// Keeps what one document translation produced, below
// <cache>/document_<checksum>/: an accepted shard as <shard>.cache.json, a shard
// whose reply was unusable as <shard>.failure.json, and the document as
// <checksum>. A cache without a checksum stores nothing.
class DocumentCache
{
public:
    // An empty rootPath selects the standard cache location.
    explicit DocumentCache(const QString& checksum = QString(), const QString& rootPath = QString());

    bool isValid() const { return !m_checksum.isEmpty(); }
    QString documentDir() const;
    // The cached copy of the document itself.
    QString documentPath() const;

    // Copies the document in, leaving a copy already there in place.
    bool storeDocument(const QString& filePath) const;

    // Answer stored for \p shard, nothing when the cache holds none.
    std::optional<QString> cachedShard(const QString& shard) const;
    bool storeShard(const QString& shard, const QString& answer) const;
    bool storeFailure(const QString& shard, const QString& answer) const;
    void removeFailure(const QString& shard) const;
    // Drops every failure the document holds.
    void removeFailures() const;

    // SHA-256 of the file contents, empty when the file cannot be read.
    static QString checksumOf(const QString& filePath);
    // Removes every document cache below \p rootPath, or below the standard
    // location when it is empty.
    static void clearAll(const QString& rootPath = QString());

private:
    static QString standardRootPath();
    QString pathFor(const QString& shard, const QString& suffix) const;
    bool write(const QString& path, const QString& text) const;

    QString m_checksum;
    QString m_rootPath;
};
