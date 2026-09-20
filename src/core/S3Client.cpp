#include "core/S3Client.h"

#include "compat/TargetProfile.h"
#include "core/Log.h"
#include "core/SigV4.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslError>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>

namespace us3 {

namespace {

/// Read a text element out of an S3 XML document at the reader's current depth.
/// S3 XML is shallow and predictable, so a streaming reader is enough.
QString readText(QXmlStreamReader &xml) {
    return xml.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
}

bool parseIso8601(const QString &s, QDateTime *out) {
    if (s.isEmpty()) {
        return false;
    }
    QDateTime dt = QDateTime::fromString(s, Qt::ISODate);
    if (!dt.isValid()) {
        dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    }
    if (!dt.isValid()) {
        return false;
    }
    dt.setTimeZone(QTimeZone::UTC);
    *out = dt;
    return true;
}

/// A tolerant ISO8601 parser for LastModified/CreationDate. Providers emit both
/// `2024-01-01T00:00:00.000Z` and `2024-01-01T00:00:00Z`, and occasionally drop
/// the zone entirely.
QDateTime parseS3Time(const QString &s) {
    QDateTime dt;
    if (parseIso8601(s, &dt)) {
        return dt;
    }
    dt = QDateTime::fromString(s, QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"));
    dt.setTimeZone(QTimeZone::UTC);
    return dt;
}

} // namespace

S3Client::S3Client(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)) {}

S3Client::~S3Client() = default;

void S3Client::configure(const S3Config &cfg) {
    m_cfg = cfg;
    m_nam->setProxy(QNetworkProxy(QNetworkProxy::DefaultProxy));
    if (Log::enabled()) {
        Log::write(Log::core(), 0,
                   QStringLiteral("configured name=\"%1\" endpoint=\"%2\" host=%3 port=%4 "
                                  "bucket=\"%5\" region=\"%6\" profile=%7 tls=%8")
                       .arg(m_cfg.name, m_cfg.endpoint, m_cfg.host(),
                            m_cfg.port().isEmpty() ? QStringLiteral("(default)") : m_cfg.port(),
                            m_cfg.bucket, m_cfg.region,
                            m_cfg.targetId.isEmpty() ? QStringLiteral("generic") : m_cfg.targetId,
                            m_cfg.effectiveUseSsl() ? QStringLiteral("https")
                                                    : QStringLiteral("http")));
    }
}

QString S3Client::effectiveRegion() const {
    if (!m_cfg.region.trimmed().isEmpty()) {
        return m_cfg.region.trimmed();
    }
    const TargetProfile profile = TargetProfile::byId(m_cfg.targetId);
    if (const QString derived = profile.regionFromEndpoint(m_cfg.host()); !derived.isEmpty()) {
        return derived;
    }
    return profile.defaultRegion;
}

QString S3Client::bucketPath(const QString &bucket) const {
    const bool virtualHost = m_cfg.addressingStyle == static_cast<int>(AddressingStyle::VirtualHost);
    return virtualHost ? QStringLiteral("/") : QStringLiteral("/") + bucket;
}

QString S3Client::objectPath(const QString &bucket, const QString &key) const {
    const QString encodedKey = QString::fromUtf8(SigV4::uriEncode(key.toUtf8(), false));
    const bool virtualHost = m_cfg.addressingStyle == static_cast<int>(AddressingStyle::VirtualHost);
    return virtualHost ? QStringLiteral("/") + encodedKey
                       : QStringLiteral("/") + bucket + QStringLiteral("/") + encodedKey;
}

QString S3Client::servicePath() const {
    return QStringLiteral("/");
}

QString S3Client::requestHost(const QString &bucketForVirtualHost) const {
    // host(), not endpoint: endpoint may still carry a scheme, and concatenating
    // that into a URL produces "https://http://host:3900/" — which QUrl accepts,
    // parses with host "http", and then fails to resolve.
    QString host = m_cfg.host();
    if (!bucketForVirtualHost.isEmpty() &&
        m_cfg.addressingStyle == static_cast<int>(AddressingStyle::VirtualHost)) {
        host = bucketForVirtualHost + QStringLiteral(".") + host;
    }

    // The port belongs in the Host header whenever it is not the scheme default.
    // This value is both the signed Host header and the authority of the URL, so
    // they cannot disagree: signing a bare host while sending "Host: host:3900"
    // is a signature mismatch that no provider explains any better than
    // "SignatureDoesNotMatch".
    if (const QString port = m_cfg.port(); !port.isEmpty() && !isDefaultPort(port)) {
        host += QStringLiteral(":") + port;
    }
    return host;
}

QString S3Client::requestUrl(const QString &path, const QByteArray &query) const {
    QString url = (m_cfg.effectiveUseSsl() ? QStringLiteral("https://") : QStringLiteral("http://")) +
                  requestHost(m_cfg.bucket) + path;
    if (!query.isEmpty()) {
        url += QStringLiteral("?") + QString::fromUtf8(query);
    }
    return url;
}

bool S3Client::isDefaultPort(const QString &port) const {
    return m_cfg.effectiveUseSsl() ? port == QLatin1String("443") : port == QLatin1String("80");
}

QNetworkReply *S3Client::send(const QByteArray &method,
                              const QString &path,
                              const QByteArray &query,
                              const QByteArray &body,
                              const QByteArray &contentType,
                              bool hashPayload) {
    const QString host = requestHost(m_cfg.bucket);
    const QString url = requestUrl(path, query);

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("us3qt/1.0"));
    req.setTransferTimeout(60000);

    if (!contentType.isEmpty()) {
        req.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromUtf8(contentType));
    }

    // TLS policy. VerifyStrict is the default; AllowSelfSigned is an explicit,
    // visible choice rather than the silent behaviour of the original.
    QSslConfiguration ssl = req.sslConfiguration();
    if (m_cfg.tls == TlsPolicy::AllowSelfSigned) {
        ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
        req.setSslConfiguration(ssl);
    }

    QByteArray payloadHash = SigV4::kUnsignedPayload;
    if (hashPayload) {
        payloadHash = SigV4::sha256Hex(body);
    }

    signRequest(req, method, path, query, payloadHash, m_cfg.bucket);

    if (Log::enabled()) {
        Log::write(Log::net(), 0,
                   QStringLiteral("%1 %2  host=%3  region=%4  profile=%5  addressing=%6")
                       .arg(QString::fromUtf8(method), url, host, effectiveRegion(),
                            m_cfg.targetId.isEmpty() ? QStringLiteral("generic") : m_cfg.targetId,
                            m_cfg.addressingStyle == int(AddressingStyle::VirtualHost)
                                ? QStringLiteral("virtual-host")
                                : QStringLiteral("path-style")));
        // DPAPI failures and a blank secret are indistinguishable from a wrong
        // key in the server's reply, so the key that was actually used is worth
        // recording. Redacted: the log is a plain file beside settings.json.
        Log::write(Log::net(), 0,
                   QStringLiteral("  access-key=%1  secret=%2  port=%3  tls=%4")
                       .arg(Log::redact(m_cfg.accessKey), Log::redact(m_cfg.secretKey),
                            m_cfg.port().isEmpty() ? QStringLiteral("(default)") : m_cfg.port(),
                            m_cfg.effectiveUseSsl() ? QStringLiteral("https")
                                                    : QStringLiteral("http")));
    }

    QNetworkReply *reply = nullptr;
    if (method == "GET") {
        reply = m_nam->get(req);
    } else if (method == "HEAD") {
        reply = m_nam->head(req);
    } else if (method == "DELETE") {
        reply = m_nam->deleteResource(req);
    } else if (method == "PUT") {
        reply = m_nam->put(req, body);
    } else if (method == "POST") {
        reply = m_nam->post(req, body);
    }
    return reply;
}

void S3Client::signRequest(QNetworkRequest &req,
                           const QByteArray &method,
                           const QString &path,
                           const QByteArray &canonicalQuery,
                           const QByteArray &payloadHash,
                           const QString &bucketForVirtualHost) const {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString amzDate = now.toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));

    SigV4::Request sreq;
    sreq.method = QString::fromUtf8(method);
    sreq.host = requestHost(bucketForVirtualHost);
    sreq.path = path;
    sreq.canonicalQuery = canonicalQuery;
    sreq.payloadHash = payloadHash;
    sreq.headers = {
        {QStringLiteral("host"), sreq.host},
        {QStringLiteral("x-amz-content-sha256"), QString::fromUtf8(payloadHash)},
        {QStringLiteral("x-amz-date"), amzDate},
    };

    const SigV4::Signed signedReq =
        SigV4::sign(sreq, m_cfg.accessKey, m_cfg.secretKey, effectiveRegion(),
                    QStringLiteral("s3"), now);

    req.setRawHeader("x-amz-date", signedReq.amzDate);
    req.setRawHeader("x-amz-content-sha256", signedReq.contentSha256);
    req.setRawHeader("Authorization", signedReq.authorization);
}

void S3Client::finish(QNetworkReply *reply, int requestId,
                      const std::function<void(const QByteArray &, const S3Error &)> &cb) {
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, cb]() {
        m_inFlight.remove(requestId);
        reply->deleteLater();

        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString url = reply->url().toString();

        if (reply->error() == QNetworkReply::OperationCanceledError) {
            logResult("cancelled", url, status, {}, S3Error::cancelled());
            cb({}, S3Error::cancelled());
            return;
        }

        // A transport error with no HTTP response at all is a network fault.
        if (status == 0 && reply->error() != QNetworkReply::NoError) {
            const S3Error error =
                S3Error::fromNetwork(static_cast<int>(reply->error()), reply->errorString());
            logResult("failed", url, status, body, error);
            cb({}, error);
            return;
        }

        if (status >= 200 && status < 300) {
            logResult("ok", url, status, {}, {});
            cb(body, {});
            return;
        }

        const QString requestIdHeader =
            QString::fromUtf8(reply->rawHeader("x-amz-request-id"));
        const S3Error error = S3Error::fromResponse(status, body, requestIdHeader);
        logResult("failed", url, status, body, error);
        cb({}, error);
    });
}

void S3Client::logResult(const char *outcome, const QString &url, int status,
                         const QByteArray &body, const S3Error &error) const {
    if (!Log::enabled()) {
        return;
    }

    if (error.isEmpty()) {
        Log::write(Log::net(), 0,
                   QStringLiteral("%1 %2 %3").arg(QString::fromLatin1(outcome),
                                                 QString::number(status), url));
        return;
    }

    Log::write(Log::net(), error.isRetryable() ? 1 : 2,
               QStringLiteral("%1 %2 %3  kind=%4  code=%5  request-id=%6  %7")
                   .arg(QString::fromLatin1(outcome), QString::number(status), url,
                        QString::fromLatin1(errorKindName(error.kind())),
                        error.serverCode().isEmpty() ? QStringLiteral("-") : error.serverCode(),
                        error.requestId().isEmpty() ? QStringLiteral("-") : error.requestId(),
                        error.message()));

    // The body is where the provider explains itself, and for a signature
    // failure it is the only place the real reason appears.
    if (!body.isEmpty()) {
        Log::write(Log::net(), 2,
                   QStringLiteral("  response body: %1").arg(Log::summariseBody(body)));
    }
}

int S3Client::listObjects(const QString &startAfter, const QString &prefix, int maxKeys,
                          const ListCallback &cb) {
    const int id = m_nextId++;

    if (const QString problem = m_cfg.validate(); !problem.isEmpty()) {
        FlatListResult r;
        r.error = S3Error::config(problem);
        cb(r);
        return id;
    }

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("list-type"), QStringLiteral("2"));
    q.addQueryItem(QStringLiteral("max-keys"), QString::number(maxKeys));
    if (!prefix.isEmpty()) {
        q.addQueryItem(QStringLiteral("prefix"), prefix);
    }
    if (!startAfter.isEmpty()) {
        q.addQueryItem(QStringLiteral("start-after"), startAfter);
    }

    // Build the canonical query ourselves: QUrlQuery would re-encode in its own
    // order, and the signature must be computed over the same bytes the server
    // will reconstruct.
    QByteArray canonicalQuery;
    const auto items = q.queryItems(QUrl::FullyEncoded);
    QStringList parts;
    parts.reserve(items.size());
    for (const auto &item : items) {
        parts.append(item.first + QLatin1Char('=') + item.second);
    }
    canonicalQuery = parts.join(QLatin1Char('&')).toUtf8();

    const QString path = bucketPath(m_cfg.bucket);
    QNetworkReply *reply = send("GET", path, canonicalQuery, {}, {}, false);
    if (!reply) {
        FlatListResult r;
        r.error = S3Error(ErrorKind::Config, QStringLiteral("Could not build the request."));
        cb(r);
        return id;
    }

    m_inFlight.insert(id, reply);

    finish(reply, id, [cb](const QByteArray &body, const S3Error &error) {
        if (!error.isEmpty()) {
            FlatListResult out;
            out.error = error;
            cb(out);
            return;
        }
        cb(parseObjectList(body));
    });

    return id;
}

int S3Client::listBuckets(const BucketsCallback &cb) {
    const int id = m_nextId++;

    if (const QString problem = m_cfg.validate(); !problem.isEmpty()) {
        Result<QList<BucketInfo>> r;
        r.error = S3Error::config(problem);
        cb(r);
        return id;
    }

    QNetworkReply *reply = send("GET", servicePath(), {}, {}, {}, false);
    m_inFlight.insert(id, reply);

    finish(reply, id, [cb](const QByteArray &body, const S3Error &error) {
        if (!error.isEmpty()) {
            Result<QList<BucketInfo>> out;
            out.error = error;
            cb(out);
            return;
        }
        cb(parseBucketList(body));
    });

    return id;
}

int S3Client::createBucket(const QString &bucket, const QString &region, const VoidCallback &cb) {
    const int id = m_nextId++;

    QByteArray body;
    const QString effectiveRegionValue = region.trimmed().isEmpty() ? effectiveRegion() : region.trimmed();
    // us-east-1 is the one region where a CreateBucketConfiguration is not
    // merely optional but rejected by AWS, so it is omitted there.
    if (effectiveRegionValue != QStringLiteral("us-east-1")) {
        body = QStringLiteral("<CreateBucketConfiguration "
                              "xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
                              "<LocationConstraint>%1</LocationConstraint>"
                              "</CreateBucketConfiguration>")
                   .arg(effectiveRegionValue)
                   .toUtf8();
    }

    QNetworkReply *reply = send("PUT", bucketPath(bucket), {}, body, "application/xml", true);
    m_inFlight.insert(id, reply);

    finish(reply, id, [cb](const QByteArray &, const S3Error &error) { cb(error); });
    return id;
}

int S3Client::deleteBucket(const QString &bucket, const VoidCallback &cb) {
    const int id = m_nextId++;
    QNetworkReply *reply = send("DELETE", bucketPath(bucket), {}, {}, {}, false);
    m_inFlight.insert(id, reply);
    finish(reply, id, [cb](const QByteArray &, const S3Error &error) { cb(error); });
    return id;
}

int S3Client::deleteObject(const QString &key, const VoidCallback &cb) {
    const int id = m_nextId++;
    QNetworkReply *reply = send("DELETE", objectPath(m_cfg.bucket, key), {}, {}, {}, false);
    m_inFlight.insert(id, reply);
    finish(reply, id, [cb](const QByteArray &, const S3Error &error) { cb(error); });
    return id;
}

int S3Client::uploadFile(const QString &localPath, const QString &objectName,
                         const std::function<void(qint64, qint64)> &onProgress,
                         const VoidCallback &cb) {
    const int id = m_nextId++;

    auto *file = new QFile(localPath, this);
    if (!file->open(QIODevice::ReadOnly)) {
        const S3Error error = S3Error(ErrorKind::Config,
                                      QStringLiteral("Cannot read %1: %2")
                                          .arg(QFileInfo(localPath).fileName(), file->errorString()));
        file->deleteLater();
        cb(error);
        return id;
    }

    const qint64 total = file->size();

    // requestUrl() rather than a second hand-built URL: the first version
    // concatenated the raw endpoint here and skipped host() entirely, so an
    // upload to a connection whose endpoint was stored with a scheme would have
    // failed exactly as the listing did.
    const QString path = objectPath(m_cfg.bucket, objectName);
    QNetworkRequest req{QUrl(requestUrl(path, {}))};
    req.setHeader(QNetworkRequest::ContentLengthHeader, total);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("us3qt/1.0"));
    req.setTransferTimeout(0); // large uploads must not be cut off mid-stream

    QSslConfiguration ssl = req.sslConfiguration();
    if (m_cfg.tls == TlsPolicy::AllowSelfSigned) {
        ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
        req.setSslConfiguration(ssl);
    }

    // Streaming uploads cannot hash the payload up front without reading the
    // file twice, so the signature declares the payload unsigned. AWS and every
    // provider we target accept this; it is the standard approach for
    // streamed PUTs.
    signRequest(req, "PUT", path, {}, QByteArray(SigV4::kUnsignedPayload), m_cfg.bucket);

    if (Log::enabled()) {
        Log::write(Log::net(), 0,
                   QStringLiteral("PUT %1  host=%2  %3 bytes  region=%4")
                       .arg(req.url().toString(), requestHost(m_cfg.bucket))
                       .arg(total)
                       .arg(effectiveRegion()));
    }

    QNetworkReply *reply = m_nam->put(req, file);
    file->setParent(reply);

    if (onProgress) {
        connect(reply, &QNetworkReply::uploadProgress, this,
                [onProgress](qint64 sent, qint64 tot) { onProgress(sent, tot); });
    }

    m_inFlight.insert(id, reply);
    finish(reply, id, [cb](const QByteArray &, const S3Error &error) { cb(error); });
    return id;
}

int S3Client::putBytes(const QByteArray &data, const QString &objectName,
                       const QString &contentType, const MaybeStringCallback &cb) {
    const int id = m_nextId++;

    const QString path = objectPath(m_cfg.bucket, objectName);
    QNetworkReply *reply = send("PUT", path, {}, data, contentType.toUtf8(), true);
    m_inFlight.insert(id, reply);

    finish(reply, id, [cb](const QByteArray &body, const S3Error &error) {
        Result<QString> out;
        if (!error.isEmpty()) {
            out.error = error;
            cb(out);
            return;
        }
        out.value = QString::fromUtf8(body);
        cb(out);
    });
    return id;
}

int S3Client::downloadObject(
    const QString &key,
    const std::function<void(bool, QNetworkReply *, const S3Error &)> &cb) {
    const int id = m_nextId++;

    QNetworkReply *reply = send("GET", objectPath(m_cfg.bucket, key), {}, {}, {}, false);
    m_inFlight.insert(id, reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, id, cb]() {
        m_inFlight.remove(id);

        if (reply->error() == QNetworkReply::OperationCanceledError) {
            logResult("cancelled", reply->url().toString(), 0, {}, S3Error::cancelled());
            reply->deleteLater();
            cb(false, nullptr, S3Error::cancelled());
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 300 && reply->error() == QNetworkReply::NoError) {
            logResult("ok", reply->url().toString(), status, {}, {});
            // Hand the open reply to the caller; it owns closing and deleting.
            cb(true, reply, {});
            return;
        }

        const QByteArray body = reply->readAll();
        const QString url = reply->url().toString();
        reply->deleteLater();

        if (status == 0) {
            const S3Error error =
                S3Error::fromNetwork(static_cast<int>(reply->error()), reply->errorString());
            logResult("failed", url, status, body, error);
            cb(false, nullptr, error);
            return;
        }

        const S3Error error = S3Error::fromResponse(status, body);
        logResult("failed", url, status, body, error);
        cb(false, nullptr, error);
    });

    return id;
}

Result<QString> S3Client::presignedUrl(const QString &key, int expiresInSeconds) const {
    Result<QString> out;

    if (const QString problem = m_cfg.validate(); !problem.isEmpty()) {
        out.error = S3Error::config(problem);
        return out;
    }

    SigV4::Request req;
    req.method = QStringLiteral("GET");
    req.host = requestHost(m_cfg.bucket);
    req.path = objectPath(m_cfg.bucket, key);

    out.value = SigV4::presignGet(req, m_cfg.accessKey, m_cfg.secretKey, effectiveRegion(),
                                  QStringLiteral("s3"), QDateTime::currentDateTimeUtc(),
                                  expiresInSeconds, m_cfg.effectiveUseSsl());
    return out;
}

void S3Client::cancel(int requestId) {
    if (QNetworkReply *reply = m_inFlight.value(requestId, nullptr)) {
        reply->abort();
    }
}

void S3Client::cancelAll() {
    const auto replies = m_inFlight;
    for (QNetworkReply *reply : replies) {
        if (reply) {
            reply->abort();
        }
    }
}

// ---------------------------------------------------------------------------
// Response parsing
//
// Kept as free functions taking the body so a fixture can be parsed directly.
// ---------------------------------------------------------------------------

FlatListResult S3Client::parseObjectList(const QByteArray &body) {
    FlatListResult out;

    QXmlStreamReader xml(body);
    QString current;
    ObjectInfo pending;
    bool inContents = false;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            current = xml.name().toString();
            if (current == QStringLiteral("Contents")) {
                pending = ObjectInfo{};
                inContents = true;
            } else if (current == QStringLiteral("IsTruncated")) {
                const QString v = readText(xml);
                out.isTruncated = (v.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            }
        } else if (xml.isCharacters() && !xml.isWhitespace() && inContents) {
            const QString text = xml.text().toString();
            if (current == QStringLiteral("Key")) {
                pending.key = text;
            } else if (current == QStringLiteral("Size")) {
                pending.size = text.toLongLong();
            } else if (current == QStringLiteral("LastModified")) {
                pending.lastModified = parseS3Time(text);
            } else if (current == QStringLiteral("ETag")) {
                pending.etag = text;
            } else if (current == QStringLiteral("StorageClass")) {
                pending.storageClass = text;
            }
        } else if (xml.isEndElement()) {
            if (xml.name() == QStringLiteral("Contents")) {
                inContents = false;
                if (!pending.key.isEmpty()) {
                    out.objects.append(pending);
                }
            }
            current.clear();
        }
    }

    if (xml.hasError() && out.objects.isEmpty()) {
        out.error = S3Error(ErrorKind::Protocol,
                            QStringLiteral("The object listing could not be parsed. "
                                           "The endpoint may not be S3-compatible."));
        return out;
    }

    if (!out.objects.isEmpty()) {
        out.nextStartAfter = out.objects.last().key;
    }
    return out;
}

Result<QList<BucketInfo>> S3Client::parseBucketList(const QByteArray &body) {
    Result<QList<BucketInfo>> out;

    QXmlStreamReader xml(body);
    QString current;
    BucketInfo pending;
    bool inBucket = false;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            current = xml.name().toString();
            if (current == QStringLiteral("Bucket")) {
                pending = BucketInfo{};
                inBucket = true;
            }
        } else if (xml.isCharacters() && !xml.isWhitespace() && inBucket) {
            const QString text = xml.text().toString();
            if (current == QStringLiteral("Name")) {
                pending.name = text;
            } else if (current == QStringLiteral("CreationDate")) {
                pending.creationDate = parseS3Time(text);
            }
        } else if (xml.isEndElement()) {
            if (xml.name() == QStringLiteral("Bucket")) {
                inBucket = false;
                if (!pending.name.isEmpty()) {
                    out.value.append(pending);
                }
            }
            current.clear();
        }
    }

    if (xml.hasError() && out.value.isEmpty()) {
        out.error = S3Error(ErrorKind::Protocol,
                            QStringLiteral("The bucket listing could not be parsed."));
    }
    return out;
}

} // namespace us3
