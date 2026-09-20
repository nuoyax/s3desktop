#include "core/S3Config.h"

#include <QRegularExpression>

namespace s3desktop {

const QString S3Config::Transient = QStringLiteral("<Transient>");

QString S3Config::schemeFromEndpoint() const {
    const QString e = endpoint.trimmed().toLower();
    const int marker = e.indexOf(QStringLiteral("://"));
    if (marker <= 0) {
        return {};
    }
    const QString scheme = e.left(marker);
    // Only the two transports this client can actually speak.
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) {
        return scheme;
    }
    return {};
}

bool S3Config::effectiveUseSsl() const {
    // An explicit scheme in the endpoint field is the most recent and most
    // specific statement of intent, so it wins.
    if (const QString scheme = schemeFromEndpoint(); !scheme.isEmpty()) {
        return scheme == QStringLiteral("https");
    }
    return useSsl || tls != TlsPolicy::Disabled;
}

QString S3Config::host() const {
    QString h = endpoint.trimmed();
    // Tolerate a pasted URL: users routinely paste "https://host" into the
    // endpoint field.
    if (h.contains(QStringLiteral("://"))) {
        h = h.mid(h.indexOf(QStringLiteral("://")) + 3);
    }
    if (const int slash = h.indexOf('/'); slash >= 0) {
        h = h.left(slash);
    }

    // IPv6 literals keep their brackets, and their internal colons are not port
    // separators — so the port, if any, is what follows the closing bracket.
    if (h.startsWith('[')) {
        const int bracket = h.indexOf(']');
        // An unterminated bracket is malformed; return it as-is and let
        // validate() reject it rather than guessing where the host ends.
        return bracket < 0 ? h : h.left(bracket + 1);
    }

    if (const int colon = h.indexOf(':'); colon >= 0) {
        return h.left(colon);
    }
    return h;
}

QString S3Config::port() const {
    const QString h = host();
    // host() reports the host (or the bracketed IPv6 literal) without its port,
    // so recover the port from the endpoint string: what follows the closing
    // bracket for a literal, or the only colon for a plain host.
    QString source = endpoint.trimmed();
    if (source.contains(QStringLiteral("://"))) {
        source = source.mid(source.indexOf(QStringLiteral("://")) + 3);
    }
    if (const int slash = source.indexOf('/'); slash >= 0) {
        source = source.left(slash);
    }

    // The host returned above is a prefix of `source`; anything after it that
    // starts with ':' is the port. Deriving it this way keeps host() and port()
    // from ever disagreeing about where the host ends.
    if (!source.startsWith(h)) {
        return {};
    }
    const QString remainder = source.mid(h.size());
    if (!remainder.startsWith(':')) {
        return {};
    }

    const QString candidate = remainder.mid(1);
    if (candidate.isEmpty() || candidate.size() > 5) {
        return {};
    }
    for (const QChar c : candidate) {
        if (!c.isDigit()) {
            return {};
        }
    }
    return candidate;
}

QString S3Config::validate() const {
    if (endpoint.trimmed().isEmpty()) {
        return QStringLiteral("An endpoint is required.");
    }
    if (host().isEmpty()) {
        return QStringLiteral("The endpoint does not contain a usable host name.");
    }
    if (accessKey.trimmed().isEmpty()) {
        return QStringLiteral("An access key is required.");
    }
    if (secretKey.isEmpty()) {
        return QStringLiteral("A secret key is required.");
    }
    if (name.trimmed().isEmpty()) {
        return QStringLiteral("Give the connection a name so it can be saved.");
    }

    // Reject an endpoint that still carries a scheme in a form we did not strip,
    // or that contains spaces — both produce confusing signature failures.
    static const QRegularExpression badHost(QStringLiteral("[\\s]"));
    if (badHost.match(host()).hasMatch()) {
        return QStringLiteral("The endpoint host contains whitespace.");
    }

    // An IPv6 literal must be bracketed. Unbracketed, its own colons are
    // indistinguishable from a port separator and host() truncates it to the
    // first group — a request sent to a host named "2001". Two colons are
    // required so that a plain "host:9000" is not caught by this.
    static const QRegularExpression unbracketedV6(
        QStringLiteral("^[0-9a-fA-F]*:[0-9a-fA-F]*:"));
    if (unbracketedV6.match(endpoint.trimmed()).hasMatch()) {
        return QStringLiteral("An IPv6 endpoint must be written in brackets, "
                              "e.g. [2001:db8::1]:9000.");
    }

    return {};
}

} // namespace s3desktop
