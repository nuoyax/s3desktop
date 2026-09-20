#include "core/S3Error.h"

#include <QHash>
#include <QNetworkReply>
#include <QXmlStreamReader>

namespace us3 {

namespace {

/// Map an S3 `<Code>` to our coarse kind. Providers invent codes freely, so the
/// default is deliberately conservative: an unknown code is treated as a server
/// error only if the HTTP status says 5xx, otherwise as a protocol problem.
ErrorKind kindForServerCode(const QString &code) {
    static const QHash<QString, ErrorKind> kTable = {
        {QStringLiteral("SignatureDoesNotMatch"), ErrorKind::Auth},
        {QStringLiteral("InvalidAccessKeyId"), ErrorKind::Auth},
        {QStringLiteral("InvalidAccessKeyID"), ErrorKind::Auth},
        {QStringLiteral("InvalidSecurity"), ErrorKind::Auth},
        {QStringLiteral("RequestTimeTooSkewed"), ErrorKind::Auth},
        {QStringLiteral("RequestExpired"), ErrorKind::Auth},
        {QStringLiteral("AuthorizationHeaderMalformed"), ErrorKind::Auth},
        {QStringLiteral("AccessDenied"), ErrorKind::AccessDenied},
        {QStringLiteral("AllAccessDisabled"), ErrorKind::AccessDenied},
        {QStringLiteral("InvalidBucketName"), ErrorKind::Config},
        {QStringLiteral("NoSuchBucket"), ErrorKind::NotFound},
        {QStringLiteral("NoSuchKey"), ErrorKind::NotFound},
        {QStringLiteral("NoSuchUpload"), ErrorKind::NotFound},
        {QStringLiteral("SlowDown"), ErrorKind::RateLimited},
        {QStringLiteral("RequestLimitExceeded"), ErrorKind::RateLimited},
        {QStringLiteral("ServiceUnavailable"), ErrorKind::ServerError},
        {QStringLiteral("InternalError"), ErrorKind::ServerError},
        {QStringLiteral("EntityTooLarge"), ErrorKind::Protocol},
        {QStringLiteral("MalformedXML"), ErrorKind::Protocol},
        {QStringLiteral("InvalidRequest"), ErrorKind::Protocol},
    };
    return kTable.value(code, ErrorKind::Unknown);
}

ErrorKind kindForStatus(int status) {
    if (status == 0) {
        return ErrorKind::Network;
    }
    if (status == 401 || status == 403) {
        return ErrorKind::AccessDenied;
    }
    if (status == 404) {
        return ErrorKind::NotFound;
    }
    if (status == 408 || status == 504) {
        return ErrorKind::Timeout;
    }
    if (status == 429) {
        return ErrorKind::RateLimited;
    }
    if (status >= 500) {
        return ErrorKind::ServerError;
    }
    if (status >= 400) {
        return ErrorKind::Protocol;
    }
    return ErrorKind::Unknown;
}

/// Pull `<Code>`, `<Message>` out of an S3 error document.
///
/// Uses a streaming reader rather than a DOM/XPath so that a truncated or
/// malformed body costs us nothing but an empty result.
struct ParsedError {
    QString code;
    QString message;
    QString requestId;
};

ParsedError parseErrorXml(const QByteArray &body) {
    ParsedError out;
    if (body.isEmpty()) {
        return out;
    }

    QXmlStreamReader xml(body);
    QString current;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            current = xml.name().toString();
        } else if (xml.isCharacters() && !xml.isWhitespace()) {
            const QString text = xml.text().toString();
            if (current == QStringLiteral("Code") && out.code.isEmpty()) {
                out.code = text;
            } else if (current == QStringLiteral("Message") && out.message.isEmpty()) {
                out.message = text;
            } else if (current == QStringLiteral("RequestId") && out.requestId.isEmpty()) {
                out.requestId = text;
            }
        } else if (xml.isEndElement()) {
            current.clear();
        }
    }
    if (xml.hasError()) {
        return {};
    }
    return out;
}

QString humanise(const QString &code) {
    static const QHash<QString, QString> kMessages = {
        {QStringLiteral("SignatureDoesNotMatch"),
         QStringLiteral("The server rejected the request signature. The secret key, "
                        "region or signature version is wrong for this provider.")},
        {QStringLiteral("InvalidAccessKeyId"),
         QStringLiteral("The server does not recognise this access key.")},
        {QStringLiteral("InvalidAccessKeyID"),
         QStringLiteral("The server does not recognise this access key.")},
        {QStringLiteral("AccessDenied"),
         QStringLiteral("Access denied. The credentials are valid but lack permission "
                        "for this operation.")},
        {QStringLiteral("RequestTimeTooSkewed"),
         QStringLiteral("The local clock differs too much from the server's. "
                        "Check the system time.")},
        {QStringLiteral("NoSuchBucket"), QStringLiteral("The bucket does not exist.")},
        {QStringLiteral("NoSuchKey"), QStringLiteral("The object does not exist.")},
        {QStringLiteral("SlowDown"),
         QStringLiteral("The provider is throttling requests. Try again shortly.")},
        {QStringLiteral("InvalidBucketName"),
         QStringLiteral("The bucket name is not valid for this provider.")},
        {QStringLiteral("AuthorizationHeaderMalformed"),
         QStringLiteral("The authorisation header was rejected. The region configured "
                        "for this connection is probably wrong.")},
    };
    return kMessages.value(code);
}

} // namespace

S3Error::S3Error(ErrorKind kind, QString message)
    : m_kind(kind), m_message(std::move(message)) {}

S3Error S3Error::fromResponse(int httpStatus, const QByteArray &body, const QString &requestId) {
    const ParsedError parsed = parseErrorXml(body);

    S3Error err;
    err.m_serverCode = parsed.code;
    err.m_serverDetail = parsed.message;
    err.m_requestId = !parsed.requestId.isEmpty() ? parsed.requestId : requestId;

    if (!parsed.code.isEmpty()) {
        err.m_kind = kindForServerCode(parsed.code);
        // An unknown code still has an HTTP status to lean on.
        if (err.m_kind == ErrorKind::Unknown) {
            err.m_kind = kindForStatus(httpStatus);
        }
        const QString friendly = humanise(parsed.code);
        // Prefer our wording; keep the server's own message as detail.
        err.m_message = !friendly.isEmpty() ? friendly : parsed.message;
        if (err.m_message.isEmpty()) {
            err.m_message = QStringLiteral("Request failed with %1").arg(parsed.code);
        }
    } else {
        err.m_kind = kindForStatus(httpStatus);
        err.m_message = QStringLiteral("The server returned HTTP %1 with no S3 error "
                                       "document. The endpoint may not be S3-compatible.")
                            .arg(httpStatus);
    }

    if (!err.m_serverDetail.isEmpty() && err.m_serverDetail != err.m_message) {
        err.m_message += QStringLiteral("\n\nServer said: %1").arg(err.m_serverDetail);
    }
    return err;
}

S3Error S3Error::fromNetwork(int qtErrorCode, const QString &qtErrorString) {
    S3Error err;
    err.m_serverDetail = qtErrorString;

    switch (qtErrorCode) {
    case QNetworkReply::OperationCanceledError:
        err.m_kind = ErrorKind::Cancelled;
        err.m_message = QStringLiteral("The operation was cancelled.");
        break;
    case QNetworkReply::TimeoutError:
    case QNetworkReply::ProxyTimeoutError:
        err.m_kind = ErrorKind::Timeout;
        err.m_message = QStringLiteral("The connection timed out. Check the endpoint "
                                       "and your network.");
        break;
    case QNetworkReply::AuthenticationRequiredError:
    case QNetworkReply::ProxyAuthenticationRequiredError:
        err.m_kind = ErrorKind::Auth;
        err.m_message = QStringLiteral("The proxy or endpoint requires authentication.");
        break;
    case QNetworkReply::HostNotFoundError:
        err.m_kind = ErrorKind::Config;
        err.m_message = QStringLiteral("The endpoint host could not be resolved. "
                                       "Check the endpoint spelling.");
        break;
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
        err.m_kind = ErrorKind::Network;
        err.m_message = QStringLiteral("The endpoint refused the connection.");
        break;
    case QNetworkReply::SslHandshakeFailedError:
        err.m_kind = ErrorKind::Config;
        err.m_message = QStringLiteral("The TLS handshake failed. The endpoint may "
                                       "expect plain HTTP, or use a certificate this "
                                       "machine does not trust.");
        break;
    default:
        err.m_kind = ErrorKind::Network;
        err.m_message = QStringLiteral("Network error: %1").arg(qtErrorString);
        break;
    }
    return err;
}

S3Error S3Error::cancelled() {
    return S3Error(ErrorKind::Cancelled, QStringLiteral("The operation was cancelled."));
}

S3Error S3Error::config(const QString &message) {
    return S3Error(ErrorKind::Config, message);
}

bool S3Error::isRetryable() const {
    switch (m_kind) {
    case ErrorKind::Network:
    case ErrorKind::Timeout:
    case ErrorKind::RateLimited:
    case ErrorKind::ServerError:
        return true;
    default:
        return false;
    }
}

bool S3Error::isConfigurationProblem() const {
    return m_kind == ErrorKind::Config || m_kind == ErrorKind::Auth;
}

QString S3Error::toString() const {
    QString out = m_message;
    if (!m_serverCode.isEmpty()) {
        out += QStringLiteral(" [%1]").arg(m_serverCode);
    }
    if (!m_requestId.isEmpty()) {
        out += QStringLiteral(" (request %1)").arg(m_requestId);
    }
    return out;
}

} // namespace us3
