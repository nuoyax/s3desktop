#pragma once

#include <QString>
#include <QStringList>

namespace us3 {

/// How requests are addressed to the server.
enum class AddressingStyle {
    PathStyle,    ///< https://endpoint/bucket/key  — the safe default
    VirtualHost,  ///< https://bucket.endpoint/key — needs wildcard DNS + cert
};

/// Which signature algorithm the provider expects.
enum class SignatureVersion {
    V4, ///< AWS Signature Version 4 — required by AWS, supported by most others
    V2, ///< legacy; still seen on older private deployments
};

/// Per-provider behaviour, kept in one place.
///
/// The original app talked to every endpoint through one MinIO client, so any
/// provider-specific quirk had to be expressed as an endpoint string hack or was
/// simply not expressible. S3-compatible providers differ in ways that are not
/// cosmetic — region derivation, addressing style and signature version all
/// change what a valid request looks like — so those differences belong in a
/// named profile rather than scattered across call sites.
struct TargetProfile {
    QString id;                 ///< stable key, e.g. "ucloud-us3"
    QString displayName;        ///< shown in the connection dialog
    AddressingStyle addressing = AddressingStyle::PathStyle;
    SignatureVersion signature = SignatureVersion::V4;
    QString defaultRegion = QStringLiteral("us-east-1");

    /// Whether this provider's bucket list endpoint is worth calling.
    /// Some deployments deny ListBuckets to ordinary users even though the
    /// bucket itself is perfectly usable, so the UI can hide what would only
    /// ever return AccessDenied.
    bool supportsListBuckets = true;

    /// Whether the provider honours `start-after` on ListObjectsV2.
    /// Where it does not, paging falls back to `marker` on the V1 API.
    bool supportsStartAfter = true;

    /// Optional hint shown under the endpoint field in the connection dialog.
    QString endpointHint;
    QString regionHint;

    /// Derive a region from the endpoint host when the user left the field
    /// blank. Returns an empty string when nothing can be inferred.
    ///
    /// This is what makes a connection work with nothing typed in the region
    /// box: signing with the wrong region produces a signature the server
    /// rejects, and the resulting error ("SignatureDoesNotMatch") gives the user
    /// no clue that the region was the problem.
    QString regionFromEndpoint(const QString &host) const;

    /// Look up a profile by id, falling back to the generic one.
    static TargetProfile byId(const QString &id);

    /// The profile used when nothing more specific is known. Path-style, SigV4,
    /// no region assumptions — the combination that the widest range of
    /// S3-compatible servers accept.
    static TargetProfile generic();

    static QList<TargetProfile> all();
};

} // namespace us3
