#pragma once

#include "core/S3Config.h"
#include "core/S3Error.h"
#include "core/S3Types.h"

#include <QByteArray>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

// Declared at global scope, not inside namespace us3, so the unqualified names
// below resolve to Qt's classes rather than to us3::QNetworkRequest.
class QNetworkAccessManager;
class QNetworkReply;

namespace us3 {

/// Result slot for one operation: a value plus an error.
///
/// Operations complete asynchronously; callbacks run on the thread that owns the
/// client (the GUI thread), so UI code can use them directly. This is the
/// structural fix for the threading problem the original app was warned about —
/// there is no path by which a worker thread touches a widget.
template <typename T>
struct Result {
    T value{};
    S3Error error;

    bool ok() const { return error.isEmpty(); }
    explicit operator bool() const { return ok(); }
};

class FlatListResult {
public:
    QList<ObjectInfo> objects;
    bool isTruncated = false;
    QString nextStartAfter;
    S3Error error;
    bool ok() const { return error.isEmpty(); }
};

/// A minimal S3 client covering exactly what the application needs.
///
/// Written against the S3 REST API rather than a vendor SDK, because the point
/// of this application is compatibility with non-AWS providers and the SDKs
/// each bake in assumptions (region handling, addressing style, XML quirks) that
/// are hard to override. Every request is built and signed here; see SigV4 for
/// the signing primitive.
///
/// All methods are asynchronous. Each returns a request id that can be passed to
/// cancel(); the callback is still invoked, with ErrorKind::Cancelled, so callers
/// need no separate cancellation path.
class S3Client : public QObject {
public:
    using ListCallback = std::function<void(const FlatListResult &)>;
    using BucketsCallback = std::function<void(const Result<QList<BucketInfo>> &)>;
    using StringCallback = std::function<void(const Result<QString> &)>;
    using VoidCallback = std::function<void(const S3Error &)>;
    using MaybeStringCallback = std::function<void(const Result<QString> &)>;

    /// Parse a ListObjectsV2 response body. Exposed so the parser can be tested
    /// without a server; the original had no way to exercise its XML handling
    /// apart from against a live bucket.
    static FlatListResult parseObjectList(const QByteArray &body);

    /// Parse a ListBuckets response body.
    static Result<QList<BucketInfo>> parseBucketList(const QByteArray &body);

    explicit S3Client(QObject *parent = nullptr);
    ~S3Client() override;

    /// Point the client at a connection. Must be called before any request.
    void configure(const S3Config &cfg);

    const S3Config &config() const { return m_cfg; }

    /// The region actually used for signing: the configured one, or one derived
    /// from the endpoint when the field was left blank.
    QString effectiveRegion() const;

    /// Whether the configuration is complete enough to attempt a request.
    bool isConfigured() const { return m_cfg.validate().isEmpty(); }

    /// List one page of objects.
    ///
    /// `startAfter` is the last key of the previous page; empty for the first
    /// page. `maxKeys` is what the server is asked for in one response.
    int listObjects(const QString &startAfter, const QString &prefix, int maxKeys,
                    const ListCallback &cb);

    int listBuckets(const BucketsCallback &cb);

    int createBucket(const QString &bucket, const QString &region, const VoidCallback &cb);
    int deleteBucket(const QString &bucket, const VoidCallback &cb);

    int deleteObject(const QString &key, const VoidCallback &cb);

    /// Upload from a file on disk.
    ///
    /// `objectName` is the destination key. Returns a request id; progress is
    /// reported through `onProgress` using -1 for "total unknown".
    int uploadFile(const QString &localPath, const QString &objectName,
                   const std::function<void(qint64 done, qint64 total)> &onProgress,
                   const VoidCallback &cb);

    /// Upload from an in-memory buffer, used by tests and small payloads.
    int putBytes(const QByteArray &data, const QString &objectName,
                 const QString &contentType, const MaybeStringCallback &cb);

    /// Start a download. The callback receives an open QIODevice that the caller
    /// must close and delete; the reply is reparented to it.
    int downloadObject(const QString &key,
                       const std::function<void(bool ok, QNetworkReply *reply, const S3Error &)> &cb);

    /// A presigned GET URL valid for `expiresInSeconds`.
    Result<QString> presignedUrl(const QString &key, int expiresInSeconds) const;

    /// Cancel an in-flight request by id. The callback fires with Cancelled.
    void cancel(int requestId);
    void cancelAll();

    /// Number of requests currently in flight.
    int inFlight() const { return m_inFlight.size(); }

private:
    /// Build a signed request for `method` on `path` and send it.
    ///
    /// `query` is the raw (unescaped) query string; encoding and sorting happen
    /// during signing so callers never handle that subtlety.
    QNetworkReply *send(const QByteArray &method,
                        const QString &path,
                        const QByteArray &query,
                        const QByteArray &body,
                        const QByteArray &contentType,
                        bool hashPayload);

    /// The path for an object, honouring the addressing style.
    QString objectPath(const QString &bucket, const QString &key) const;
    QString bucketPath(const QString &bucket) const;
    QString servicePath() const;

    /// Host header value: the endpoint host, plus the bucket in virtual-host
    /// mode, plus a non-default port. Also the authority of every URL, so the
    /// Host header and the URL can never disagree.
    QString requestHost(const QString &bucketForVirtualHost = {}) const;

    /// The full URL for `path`, with the scheme and the host above. Built in one
    /// place: the first version assembled URLs at four call sites and one of them
    /// did not go through host() at all.
    QString requestUrl(const QString &path, const QByteArray &query) const;

    /// Whether `port` is the default for the configured scheme, and so should be
    /// left out of the Host header.
    bool isDefaultPort(const QString &port) const;

    /// Attach the signing headers to an outgoing request.
    void signRequest(QNetworkRequest &req,
                     const QByteArray &method,
                     const QString &path,
                     const QByteArray &canonicalQuery,
                     const QByteArray &payloadHash,
                     const QString &bucketForVirtualHost) const;

    /// Read the body and hand back either the payload or a mapped error.
    void finish(QNetworkReply *reply, int requestId,
                const std::function<void(const QByteArray &body, const S3Error &error)> &cb);

    /// One line per completed request, plus the response body when it failed.
    /// A no-op when logging is off, before any string is built.
    void logResult(const char *outcome, const QString &url, int status,
                   const QByteArray &body, const S3Error &error) const;

    QNetworkAccessManager *m_nam;
    S3Config m_cfg;
    QHash<int, QNetworkReply *> m_inFlight;
    int m_nextId = 1;
};

} // namespace us3
