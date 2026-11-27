#pragma once

#include <QString>

#include <optional>

// Answers of document windows, one file per request in the document cache:
// document/<sample>.json holds the answer of the request named by <sample>,
// document/failure/<sample>.json the reply of a request that could not be used.
// <sample> is the full digest of the request, so any document sending the same
// request is answered from the cache; files are addressed by that name alone, and a
// record no request asks for again stays until the cache is cleared.
class DocumentCache
{
public:
    // An empty rootPath selects the document directory in the standard cache.
    explicit DocumentCache(const QString& rootPath = QString());

    QString rootPath() const { return m_rootPath; }

    std::optional<QString> cachedAnswer(const QString& sample) const;
    bool storeAnswer(const QString& sample, const QString& answer) const;
    bool storeFailure(const QString& sample, const QString& reply) const;
    void removeFailure(const QString& sample) const;

    static void clearAll(const QString& rootPath = QString());

private:
    static QString standardRootPath();
    QString answerPath(const QString& sample) const;
    QString failurePath(const QString& sample) const;
    bool write(const QString& path, const QString& text) const;

    QString m_rootPath;
};
