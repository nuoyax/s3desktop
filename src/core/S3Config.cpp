#include "core/S3Config.h"

#include <QRegularExpression>

namespace us3 {

const QString S3Config::Transient = QStringLiteral("<Transient>");

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
    // IPv6 literals keep their brackets.
    if (h.startsWith('[')) {
        return h;
    }
    if (const int colon = h.indexOf(':'); colon >= 0) {
        return h.left(colon);
    }
    return h;
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

    return {};
}

} // namespace us3
