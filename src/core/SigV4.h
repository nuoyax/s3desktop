#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>

namespace s3desktop {

/// AWS Signature Version 4, implemented against the published algorithm.
///
/// Verified in tests/test_sigv4.cpp against the worked example in the AWS
/// "Signature Version 4 signing process" documentation, so the primitive is
/// known-good before anything is built on top of it.
///
/// Qt's QMessageAuthenticationCode provides HMAC-SHA256; no external crypto
/// dependency is needed.
class SigV4 {
public:
    /// Everything that goes into the canonical request.
    struct Request {
        QString method;                 ///< GET, PUT, DELETE, HEAD
        QString host;                   ///< host[:port], no scheme
        QString path;                   ///< already URI-encoded, leading '/'
        QByteArray canonicalQuery;      ///< already sorted and encoded
        QList<QPair<QString, QString>> headers; ///< values are trimmed
        QByteArray payloadHash;         ///< SHA256 of the body, or UNSIGNED-PAYLOAD
    };

    /// Result of signing: the headers to attach and the signature itself.
    struct Signed {
        QByteArray authorization;       ///< full Authorization header value
        QByteArray amzDate;             ///< ISO8601 basic, e.g. 20150830T123600Z
        QByteArray contentSha256;       ///< hex digest, or UNSIGNED-PAYLOAD
    };

    /// Sign a request with the given long-term credentials.
    ///
    /// `region` and `service` default to the values that work for the widest
    /// range of S3-compatible services ("us-east-1" / "s3"), which is also what
    /// MinIO clients send when the endpoint does not declare a region.
    static Signed sign(const Request &req,
                       const QString &accessKey,
                       const QString &secretKey,
                       const QString &region,
                       const QString &service,
                       const QDateTime &when);

    /// Build a presigned GET URL, valid for `expiresInSeconds`.
    ///
    /// Returns the full URL including scheme. The caller supplies the scheme
    /// because it lives in the connection config, not in the signature.
    static QString presignGet(const Request &req,
                              const QString &accessKey,
                              const QString &secretKey,
                              const QString &region,
                              const QString &service,
                              const QDateTime &when,
                              int expiresInSeconds,
                              bool useSsl);

    // --- building blocks, exposed for testing ---

    /// URI-encode per AWS rules: unreserved characters are A-Z a-z 0-9 - _ . ~
    /// and everything else becomes %XX uppercase. `encodeSlash` controls
    /// whether '/' survives, which differs between the path and the query.
    static QByteArray uriEncode(const QByteArray &input, bool encodeSlash);

    /// Hex SHA256 of `data`.
    static QByteArray sha256Hex(const QByteArray &data);

    /// HMAC-SHA256.
    static QByteArray hmacSha256(const QByteArray &key, const QByteArray &msg);

    static const char *kUnsignedPayload; ///< "UNSIGNED-PAYLOAD"
};

} // namespace s3desktop
