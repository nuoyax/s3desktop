#pragma once

#include <QString>

namespace us3 {

/// TLS policy for a connection.
///
/// The original app exposed a single "Use SSL" checkbox, which on Windows
/// silently accepted any certificate — including self-signed ones — so a user
/// had no way to tell a valid endpoint from one behind an intercepting proxy.
/// The three-way choice here keeps the convenient default without hiding the
/// distinction.
enum class TlsPolicy {
    Disabled,       ///< plain http
    VerifyStrict,   ///< https, certificate chain must verify (default)
    AllowSelfSigned ///< https, verification errors are accepted and logged
};

/// One saved connection profile.
///
/// Field names and the on-disk JSON keys deliberately match the original app so
/// an existing settings.json keeps working after switching to this build. The
/// transient connection is identified by the same sentinel name.
class S3Config {
public:
    /// Sentinel name for a connection assembled from CLI flags or the
    /// environment rather than loaded from disk. Never persisted.
    static const QString Transient;

    QString name;
    QString endpoint;   ///< host or host:port, no scheme
    QString accessKey;
    QString secretKey;  ///< empty on disk once stored in the credential store
    QString bucket;
    QString prefix;
    QString region;     ///< empty means "derive from endpoint"
    bool useSsl = false;

    /// Which behaviour profile this connection targets.
    QString targetId;

    /// How the bucket is addressed. Defaults to path-style because that is what
    /// private and non-AWS deployments expect.
    int addressingStyle = 0; ///< mirrors AddressingStyle

    /// TLS handling; derived from useSsl for connections saved by the original
    /// app, which had no such field.
    TlsPolicy tls = TlsPolicy::VerifyStrict;

    /// The scheme written into `endpoint`, lowercased: "http", "https", or empty
    /// when the endpoint was given as a bare host.
    QString schemeFromEndpoint() const;

    /// The effective TLS policy, reconciling the legacy `useSsl` flag and any
    /// scheme the user typed into the endpoint field.
    ///
    /// An explicit scheme wins over the checkbox. A user who types
    /// "http://10.0.0.5:3900" has stated the transport twice, and honouring the
    /// checkbox instead sends an https request to a plain-HTTP service — which
    /// fails as a TLS handshake error that names neither the field nor the
    /// setting responsible.
    bool effectiveUseSsl() const;

    /// Host portion of `endpoint`, without any port.
    QString host() const;

    /// Port from `endpoint`, or empty when none was given. Kept separate from
    /// host() so the signed Host header stays bare while the URL keeps the port:
    /// signing "host:3900" is rejected by every provider, and dropping the port
    /// entirely reaches the wrong service.
    QString port() const;

    /// Validate before any request is attempted, so the user gets a precise
    /// complaint instead of a signature error from the server.
    /// Returns an empty string when the configuration is usable.
    QString validate() const;
};

} // namespace us3
