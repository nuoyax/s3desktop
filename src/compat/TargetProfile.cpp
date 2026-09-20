#include "compat/TargetProfile.h"

#include <QRegularExpression>

namespace s3desktop {

namespace {

/// UCloud US3 endpoints look like `s3-<region>.ufileos.com`, and the region in
/// the hostname is exactly the region the signer must use.
QString ucloudRegionFromHost(const QString &host) {
    static const QRegularExpression re(QStringLiteral("^s3-([a-z0-9-]+)\\.ufileos\\.com$"));
    const auto m = re.match(host);
    if (m.hasMatch()) {
        return m.captured(1);
    }
    return {};
}

/// AWS style: `s3.<region>.amazonaws.com` or `s3-<region>.amazonaws.com`.
QString awsRegionFromHost(const QString &host) {
    static const QRegularExpression re(
        QStringLiteral("^s3[.-]([a-z0-9-]+)\\.amazonaws\\.com(?:\\.cn)?$"));
    const auto m = re.match(host);
    if (m.hasMatch()) {
        const QString region = m.captured(1);
        // `s3.amazonaws.com` (no region) and `s3-external-1` both mean
        // us-east-1 in practice.
        if (region == QStringLiteral("external-1") || region == QStringLiteral("amazonaws")) {
            return QStringLiteral("us-east-1");
        }
        return region;
    }
    return {};
}

} // namespace

QString TargetProfile::regionFromEndpoint(const QString &host) const {
    if (host.isEmpty()) {
        return {};
    }

    if (id == QStringLiteral("ucloud-us3")) {
        if (const QString r = ucloudRegionFromHost(host); !r.isEmpty()) {
            return r;
        }
    }
    if (id == QStringLiteral("aws-s3")) {
        if (const QString r = awsRegionFromHost(host); !r.isEmpty()) {
            return r;
        }
    }

    // A generic attempt that helps more often than it hurts: `s3-<region>.host`
    // and `s3.<region>.host` are common conventions across providers.
    static const QRegularExpression re(QStringLiteral("^s3[.-]([a-z]{2}-[a-z0-9-]+)\\."));
    if (const auto m = re.match(host); m.hasMatch()) {
        return m.captured(1);
    }

    return {};
}

TargetProfile TargetProfile::generic() {
    TargetProfile p;
    p.id = QStringLiteral("generic");
    p.displayName = QStringLiteral("Generic S3-compatible");
    p.addressing = AddressingStyle::PathStyle;
    p.signature = SignatureVersion::V4;
    p.defaultRegion = QStringLiteral("us-east-1");
    p.supportsListBuckets = true;
    p.supportsStartAfter = true;
    p.endpointHint = QStringLiteral("host or host:port, e.g. s3.example.com");
    p.regionHint = QStringLiteral("Leave blank to derive from the endpoint");
    return p;
}

QList<TargetProfile> TargetProfile::all() {
    QList<TargetProfile> out;

    out.append(generic());

    {
        TargetProfile p = generic();
        p.id = QStringLiteral("aws-s3");
        p.displayName = QStringLiteral("Amazon S3");
        p.defaultRegion = QStringLiteral("us-east-1");
        p.endpointHint = QStringLiteral("s3.<region>.amazonaws.com");
        p.regionHint = QStringLiteral("Required for AWS; e.g. eu-central-1");
        out.append(p);
    }

    {
        TargetProfile p = generic();
        p.id = QStringLiteral("ucloud-us3");
        p.displayName = QStringLiteral("UCloud US3");
        p.defaultRegion = QStringLiteral("cn-bj");
        p.endpointHint = QStringLiteral("s3-<region>.ufileos.com  (e.g. s3-cn-bj.ufileos.com)");
        p.regionHint = QStringLiteral("Derived from the endpoint; e.g. cn-bj, cn-sh2, hk");
        out.append(p);
    }

    {
        TargetProfile p = generic();
        p.id = QStringLiteral("minio");
        p.displayName = QStringLiteral("MinIO / Ceph RGW");
        p.defaultRegion = QStringLiteral("us-east-1");
        p.endpointHint = QStringLiteral("host:port, e.g. minio.local:9000");
        p.regionHint = QStringLiteral("Usually unnecessary");
        out.append(p);
    }

    return out;
}

TargetProfile TargetProfile::byId(const QString &id) {
    const QList<TargetProfile> profiles = all();
    for (const TargetProfile &p : profiles) {
        if (p.id == id) {
            return p;
        }
    }
    return generic();
}

} // namespace s3desktop
