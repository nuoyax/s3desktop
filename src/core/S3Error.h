#pragma once

#include "core/S3Types.h"

#include <QByteArray>
#include <QString>

namespace us3 {

/// A failure that carries enough structure for the UI to react sensibly:
/// whether to retry, what to tell the user, and the raw server code.
///
/// The original app surfaced minio-go's error strings directly, which for a
/// Chinese S3 provider often produces an opaque XML blob in a dialog. Here the
/// server's `<Code>` is parsed and mapped to a plain-language message, with the
/// raw detail kept in `serverDetail` for the logs.
class S3Error {
public:
    S3Error() = default;
    S3Error(ErrorKind kind, QString message);

    /// Build from an HTTP status plus an S3 XML error body.
    ///
    /// `body` may be anything: a well-formed S3 error document, an HTML proxy
    /// error page, or empty. Anything unparseable degrades to a status-derived
    /// message rather than throwing.
    static S3Error fromResponse(int httpStatus, const QByteArray &body, const QString &requestId = {});

    /// Build from a Qt network error.
    static S3Error fromNetwork(int qtErrorCode, const QString &qtErrorString);

    static S3Error cancelled();
    static S3Error config(const QString &message);

    ErrorKind kind() const { return m_kind; }
    QString message() const { return m_message; }

    /// The server's `<Code>` element, e.g. "SignatureDoesNotMatch". Empty when
    /// the response carried no parseable S3 error document.
    QString serverCode() const { return m_serverCode; }

    /// The server's `<Message>` element, if any.
    QString serverDetail() const { return m_serverDetail; }

    /// X-Amz-Request-Id / x-amz-request-id, useful when a provider asks for it.
    QString requestId() const { return m_requestId; }

    /// Whether an identical request has a reasonable chance of succeeding.
    bool isRetryable() const;

    /// Whether this failure suggests the connection profile is wrong (endpoint,
    /// region or signature scheme) rather than the request being bad.
    bool isConfigurationProblem() const;

    bool isEmpty() const { return m_kind == ErrorKind::None; }

    QString toString() const;

private:
    ErrorKind m_kind = ErrorKind::None;
    QString m_message;
    QString m_serverCode;
    QString m_serverDetail;
    QString m_requestId;
};

} // namespace us3
