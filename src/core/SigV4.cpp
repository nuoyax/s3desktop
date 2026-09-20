#include "core/SigV4.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QList>
#include <QMessageAuthenticationCode>
#include <QPair>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>

namespace us3 {

const char *SigV4::kUnsignedPayload = "UNSIGNED-PAYLOAD";

QByteArray SigV4::sha256Hex(const QByteArray &data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

QByteArray SigV4::hmacSha256(const QByteArray &key, const QByteArray &msg) {
    return QMessageAuthenticationCode::hash(msg, key, QCryptographicHash::Sha256);
}

QByteArray SigV4::uriEncode(const QByteArray &input, bool encodeSlash) {
    static const char *kHex = "0123456789ABCDEF";

    QByteArray out;
    out.reserve(input.size() * 3);

    for (char ch : input) {
        const unsigned char c = static_cast<unsigned char>(ch);
        const bool unreserved =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~';

        if (unreserved || (c == '/' && !encodeSlash)) {
            out.append(static_cast<char>(c));
        } else {
            out.append('%');
            out.append(kHex[(c >> 4) & 0x0F]);
            out.append(kHex[c & 0x0F]);
        }
    }
    return out;
}

namespace {

/// Canonical query string: every parameter URI-encoded (including '/'), sorted
/// by encoded key then encoded value, joined with '&'.
QByteArray canonicalQueryString(const QByteArray &rawQuery) {
    if (rawQuery.isEmpty()) {
        return {};
    }

    QList<QPair<QByteArray, QByteArray>> pairs;
    const QList<QByteArray> parts = rawQuery.split('&');
    for (const QByteArray &part : parts) {
        if (part.isEmpty()) {
            continue;
        }
        const int eq = part.indexOf('=');
        QByteArray key = eq >= 0 ? part.left(eq) : part;
        QByteArray value = eq >= 0 ? part.mid(eq + 1) : QByteArray();

        // Values arrive percent-encoded; decode first so we re-encode exactly
        // once. Double-encoding here is the classic SigV4 signature mismatch.
        pairs.append({SigV4::uriEncode(QUrl::fromPercentEncoding(key).toUtf8(), true),
                      SigV4::uriEncode(QUrl::fromPercentEncoding(value).toUtf8(), true)});
    }

    std::sort(pairs.begin(), pairs.end(), [](const auto &a, const auto &b) {
        if (a.first != b.first) {
            return a.first < b.first;
        }
        return a.second < b.second;
    });

    QByteArray out;
    for (int i = 0; i < pairs.size(); ++i) {
        if (i > 0) {
            out.append('&');
        }
        out.append(pairs[i].first);
        out.append('=');
        out.append(pairs[i].second);
    }
    return out;
}

/// Collapse every run of ASCII whitespace to a single space and trim. SigV4
/// requires header values folded this way before hashing.
QByteArray collapseWhitespace(const QByteArray &raw) {
    QByteArray out;
    out.reserve(raw.size());
    bool pendingSpace = false;
    bool wroteAny = false;

    for (char ch : raw) {
        const bool isSpace = ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        if (isSpace) {
            pendingSpace = wroteAny;
            continue;
        }
        if (pendingSpace) {
            out.append(' ');
            pendingSpace = false;
        }
        out.append(ch);
        wroteAny = true;
    }
    return out;
}

/// Canonical headers, already sorted by lowercase name, each folded to a single
/// line with runs of whitespace collapsed to one space.
QByteArray canonicalHeaders(const QList<QPair<QString, QString>> &headers, QByteArray *signedNames) {
    QList<QPair<QByteArray, QByteArray>> normalised;
    normalised.reserve(headers.size());

    for (const auto &h : headers) {
        QByteArray name = h.first.toLower().trimmed().toUtf8();
        QByteArray value = collapseWhitespace(h.second.toUtf8());
        normalised.append({name, value});
    }

    std::sort(normalised.begin(), normalised.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    QByteArray out;
    QByteArray names;
    for (const auto &h : normalised) {
        out.append(h.first);
        out.append(':');
        out.append(h.second);
        out.append('\n');
        if (!names.isEmpty()) {
            names.append(';');
        }
        names.append(h.first);
    }
    *signedNames = names;
    return out;
}

QByteArray dateStamp(const QDateTime &whenUtc) {
    return whenUtc.toString("yyyyMMdd").toUtf8();
}

QByteArray amzDateStamp(const QDateTime &whenUtc) {
    return whenUtc.toString("yyyyMMdd'T'HHmmss'Z'").toUtf8();
}

/// The key derivation chain: kDate -> kRegion -> kService -> kSigning.
QByteArray signingKey(const QString &secretKey, const QByteArray &date,
                      const QString &region, const QString &service) {
    const QByteArray kDate = SigV4::hmacSha256("AWS4" + secretKey.toUtf8(), date);
    const QByteArray kRegion = SigV4::hmacSha256(kDate, region.toUtf8());
    const QByteArray kService = SigV4::hmacSha256(kRegion, service.toUtf8());
    return SigV4::hmacSha256(kService, "aws4_request");
}

QString stringToSign(const QByteArray &amzDate, const QByteArray &dateStampValue,
                     const QString &region, const QString &service,
                     const QByteArray &canonicalRequestHash) {
    return QStringLiteral("AWS4-HMAC-SHA256\n%1\n%2/%3/%4/aws4_request\n%5")
        .arg(QString::fromUtf8(amzDate),
             QString::fromUtf8(dateStampValue),
             region,
             service,
             QString::fromUtf8(canonicalRequestHash));
}

} // namespace

SigV4::Signed SigV4::sign(const Request &req,
                          const QString &accessKey,
                          const QString &secretKey,
                          const QString &region,
                          const QString &service,
                          const QDateTime &when) {
    const QDateTime whenUtc = when.toUTC();
    const QByteArray amzDate = amzDateStamp(whenUtc);
    const QByteArray date = dateStamp(whenUtc);

    QByteArray signedNames;
    QByteArray headers = canonicalHeaders(req.headers, &signedNames);

    const QByteArray payloadHash =
        req.payloadHash.isEmpty() ? QByteArray(kUnsignedPayload) : req.payloadHash;

    QByteArray canonicalRequest;
    canonicalRequest.append(req.method.toUtf8());
    canonicalRequest.append('\n');
    canonicalRequest.append(req.path.toUtf8());
    canonicalRequest.append('\n');
    canonicalRequest.append(canonicalQueryString(req.canonicalQuery));
    canonicalRequest.append('\n');
    canonicalRequest.append(headers);
    canonicalRequest.append('\n');
    canonicalRequest.append(signedNames);
    canonicalRequest.append('\n');
    canonicalRequest.append(payloadHash);

    const QByteArray canonicalHash = sha256Hex(canonicalRequest);
    const QString sts = stringToSign(amzDate, date, region, service, canonicalHash);

    const QByteArray kSigning = signingKey(secretKey, date, region, service);
    const QByteArray signature = hmacSha256(kSigning, sts.toUtf8()).toHex();

    Signed out;
    out.amzDate = amzDate;
    out.contentSha256 = payloadHash;
    out.authorization =
        QStringLiteral("AWS4-HMAC-SHA256 Credential=%1/%2/%3/%4/aws4_request, "
                       "SignedHeaders=%5, Signature=%6")
            .arg(accessKey,
                 QString::fromUtf8(date),
                 region,
                 service,
                 QString::fromUtf8(signedNames),
                 QString::fromUtf8(signature))
            .toUtf8();
    return out;
}

QString SigV4::presignGet(const Request &req,
                          const QString &accessKey,
                          const QString &secretKey,
                          const QString &region,
                          const QString &service,
                          const QDateTime &when,
                          int expiresInSeconds,
                          bool useSsl) {
    const QDateTime whenUtc = when.toUTC();
    const QByteArray amzDate = amzDateStamp(whenUtc);
    const QByteArray date = dateStamp(whenUtc);
    const QByteArray scope =
        QStringLiteral("%1/%2/%3/aws4_request").arg(QString::fromUtf8(date), region, service).toUtf8();

    // Query parameters that participate in the signature. X-Amz-Signature is
    // appended after signing and is therefore absent here.
    QByteArray query = req.canonicalQuery;
    if (!query.isEmpty()) {
        query.append('&');
    }
    query.append("X-Amz-Algorithm=AWS4-HMAC-SHA256");
    query.append("&X-Amz-Credential=");
    query.append(uriEncode(accessKey.toUtf8() + "/" + scope, true));
    query.append("&X-Amz-Date=");
    query.append(amzDate);
    query.append("&X-Amz-Expires=");
    query.append(QByteArray::number(expiresInSeconds));
    query.append("&X-Amz-SignedHeaders=host");

    // Canonical request uses the sorted-encoded query; signing must see the
    // same ordering the server will recompute.
    const QByteArray sortedQuery = canonicalQueryString(query);

    QByteArray canonicalRequest;
    canonicalRequest.append(req.method.toUtf8());
    canonicalRequest.append('\n');
    canonicalRequest.append(req.path.toUtf8());
    canonicalRequest.append('\n');
    canonicalRequest.append(sortedQuery);
    canonicalRequest.append('\n');
    canonicalRequest.append("host:");
    canonicalRequest.append(req.host.toUtf8());
    canonicalRequest.append("\n\nhost\n");
    canonicalRequest.append(kUnsignedPayload);

    const QByteArray canonicalHash = sha256Hex(canonicalRequest);
    const QString sts = stringToSign(amzDate, date, region, service, canonicalHash);

    const QByteArray signature = hmacSha256(signingKey(secretKey, date, region, service), sts.toUtf8()).toHex();

    const QString scheme = useSsl ? QStringLiteral("https") : QStringLiteral("http");
    return QStringLiteral("%1://%2%3?%4&X-Amz-Signature=%5")
        .arg(scheme, req.host, req.path, QString::fromUtf8(sortedQuery), QString::fromUtf8(signature));
}

} // namespace us3
