#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

namespace s3desktop {

/// One object as returned by a ListObjectsV2 page.
struct ObjectInfo {
    QString key;
    qint64 size = 0;
    QDateTime lastModified;
    QString etag;
    QString storageClass;
};

/// One bucket as returned by ListBuckets.
struct BucketInfo {
    QString name;
    QDateTime creationDate;
};

/// A page of a ListObjectsV2 response.
///
/// `isTruncated` is what drives "Load more"; `nextStartAfter` is the key to
/// resume from. We deliberately do not use continuation tokens: MinIO and
/// several non-AWS implementations emit `NextContinuationToken` that is a
/// re-encoded opaque blob, and `start-after` is the one resume mechanism all of
/// them agree on.
struct ListPage {
    QList<ObjectInfo> objects;
    bool isTruncated = false;
    QString nextStartAfter;
};

/// What went wrong, in a form the UI can act on.
enum class ErrorKind {
    None,
    Network,        ///< DNS, TCP, TLS — retrying may help
    Timeout,
    Auth,           ///< 401/403 or a signature-related S3 error code
    NotFound,       ///< 404 / NoSuchKey / NoSuchBucket
    AccessDenied,
    RateLimited,    ///< 429/503 SlowDown — retrying with backoff helps
    ServerError,    ///< 5xx
    Protocol,       ///< response did not parse as S3 XML
    Cancelled,
    Config,         ///< bad endpoint/credentials before any request went out
    Unknown,
};

/// Progress of a single transfer.
struct TransferProgress {
    qint64 bytesDone = 0;
    qint64 bytesTotal = 0;
    double fraction() const {
        return bytesTotal > 0 ? static_cast<double>(bytesDone) / static_cast<double>(bytesTotal) : 0.0;
    }
};

} // namespace s3desktop
