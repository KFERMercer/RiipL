#pragma once

#include <QString>

#include <optional>

// Answers of document windows, one file per request below the cache location:
// <sample>.document.cache.json holds the answer of the request named by <sample>,
// <sample>.document.failure.json the reply of a request that could not be used.
// <sample> is the full digest of the request, so any document sending the same
// request is answered from the cache; files are addressed by that name alone, and a
// record no request asks for again stays until the cache is cleared.
class DocumentCache
{
public:
    // An empty rootPath selects the standard cache location.
    explicit DocumentCache(const QString& rootPath = QString());

    QString rootPath() const { return m_rootPath; }

    // Answer stored for \p sample, nothing when the cache holds none.
    std::optional<QString> cachedAnswer(const QString& sample) const;
    bool storeAnswer(const QString& sample, const QString& answer) const;
    bool storeFailure(const QString& sample, const QString& reply) const;
    void removeFailure(const QString& sample) const;

    // Removes every document cache file below \p rootPath, or below the standard
    // location when it is empty.
    static void clearAll(const QString& rootPath = QString());

private:
    static QString standardRootPath();
    // Path of the file \p sample names.
    QString pathFor(const QString& sample, const QString& suffix) const;
    bool write(const QString& path, const QString& text) const;

    QString m_rootPath;
};
