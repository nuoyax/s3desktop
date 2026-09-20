#include "compat/TargetProfile.h"
#include "core/S3Config.h"

#include <QTest>

using namespace us3;

/// Provider profiles and connection validation.
///
/// The region derived here is not cosmetic: signing with the wrong region
/// produces `SignatureDoesNotMatch`, and that error tells the user nothing about
/// the real cause. These cases pin down the derivation.
class TestTargetProfile : public QObject {
    Q_OBJECT

private slots:
    void genericIsPathStyleAndSigV4();
    void derivesRegionFromUCloudEndpoint();
    void derivesRegionFromAwsEndpoint();
    void returnsEmptyRegionForAnUnknownHost();
    void unknownProfileFallsBackToGeneric();
    void everyProfileRoundTripsById();
    void validationRejectsIncompleteConfigurations();
    void acceptsAnEndpointWithAPort();
    void stripsASchemeFromAPastedEndpoint();
    void transientSentinelNeverEqualsARealName();
    void legacyTlsFlagReconcilesWithTheNewPolicy();
};

void TestTargetProfile::genericIsPathStyleAndSigV4() {
    const TargetProfile profile = TargetProfile::generic();

    // Path style is the combination the widest range of S3-compatible servers
    // accept without wildcard DNS or a wildcard certificate.
    QCOMPARE(profile.addressing, AddressingStyle::PathStyle);
    QCOMPARE(profile.signature, SignatureVersion::V4);
    QVERIFY(!profile.defaultRegion.isEmpty());
}

void TestTargetProfile::derivesRegionFromUCloudEndpoint() {
    const TargetProfile us3 = TargetProfile::byId(QStringLiteral("ucloud-us3"));

    // s3-<region>.ufileos.com is the pattern UCloud's own console hands out.
    QCOMPARE(us3.regionFromEndpoint(QStringLiteral("s3-cn-bj.ufileos.com")),
             QStringLiteral("cn-bj"));
    QCOMPARE(us3.regionFromEndpoint(QStringLiteral("s3-cn-sh2.ufileos.com")),
             QStringLiteral("cn-sh2"));
    QCOMPARE(us3.regionFromEndpoint(QStringLiteral("s3-hk.ufileos.com")), QStringLiteral("hk"));

    // A host that does not match falls back to nothing rather than to a guess,
    // so the profile's default region is used and the failure mode is a clear
    // signature error rather than a silent wrong-region request.
    QVERIFY(us3.regionFromEndpoint(QStringLiteral("storage.example.com")).isEmpty());
}

void TestTargetProfile::derivesRegionFromAwsEndpoint() {
    const TargetProfile aws = TargetProfile::byId(QStringLiteral("aws-s3"));

    QCOMPARE(aws.regionFromEndpoint(QStringLiteral("s3.eu-west-1.amazonaws.com")),
             QStringLiteral("eu-west-1"));
    QCOMPARE(aws.regionFromEndpoint(QStringLiteral("s3.ap-southeast-2.amazonaws.com")),
             QStringLiteral("ap-southeast-2"));
}

void TestTargetProfile::returnsEmptyRegionForAnUnknownHost() {
    const TargetProfile generic = TargetProfile::generic();

    QVERIFY(generic.regionFromEndpoint(QStringLiteral("minio.internal")).isEmpty());
    QVERIFY(generic.regionFromEndpoint(QString()).isEmpty());
}

void TestTargetProfile::unknownProfileFallsBackToGeneric() {
    const TargetProfile unknown = TargetProfile::byId(QStringLiteral("no-such-provider"));

    // Silently falling back is right here: a settings.json written by a newer
    // build, or hand-edited, must not make the app refuse to start.
    QCOMPARE(unknown.addressing, AddressingStyle::PathStyle);
    QCOMPARE(unknown.id, TargetProfile::generic().id);
}

void TestTargetProfile::everyProfileRoundTripsById() {
    const QList<TargetProfile> all = TargetProfile::all();
    QVERIFY(!all.isEmpty());

    for (const TargetProfile &profile : all) {
        // An empty id would make byId() fall back for a profile that exists, so
        // the list and the lookup would disagree.
        QVERIFY(!profile.id.isEmpty());
        QVERIFY(!profile.displayName.isEmpty());
        QCOMPARE(TargetProfile::byId(profile.id).id, profile.id);
    }
}

void TestTargetProfile::validationRejectsIncompleteConfigurations() {
    S3Config config;
    QVERIFY(!config.validate().isEmpty());

    config.name = QStringLiteral("test");
    QVERIFY(!config.validate().isEmpty());

    config.endpoint = QStringLiteral("s3-cn-bj.ufileos.com");
    QVERIFY(!config.validate().isEmpty());

    config.accessKey = QStringLiteral("AKIAEXAMPLE");
    QVERIFY(!config.validate().isEmpty());

    config.secretKey = QStringLiteral("secret");
    QVERIFY(config.validate().isEmpty());
}

void TestTargetProfile::acceptsAnEndpointWithAPort() {
    S3Config config;
    config.name = QStringLiteral("minio");
    config.endpoint = QStringLiteral("10.0.0.5:9000");
    config.accessKey = QStringLiteral("minioadmin");
    config.secretKey = QStringLiteral("minioadmin");

    QVERIFY(config.validate().isEmpty());
    // host() strips the port: the Host header must carry it, but the region
    // derivation and TLS decisions must not.
    QCOMPARE(config.host(), QStringLiteral("10.0.0.5"));
}

void TestTargetProfile::stripsASchemeFromAPastedEndpoint() {
    S3Config config;
    config.name = QStringLiteral("pasted");
    config.endpoint = QStringLiteral("https://s3-cn-bj.ufileos.com/some/path");
    config.accessKey = QStringLiteral("key");
    config.secretKey = QStringLiteral("secret");

    QVERIFY(config.validate().isEmpty());
    QCOMPARE(config.host(), QStringLiteral("s3-cn-bj.ufileos.com"));
}

void TestTargetProfile::transientSentinelNeverEqualsARealName() {
    // The sentinel is written to settings.json as a name for the environment
    // connection. If a user could type it, a transient profile could be saved
    // over a real one.
    QVERIFY(!S3Config::Transient.isEmpty());
    QVERIFY(S3Config::Transient.contains(QLatin1Char('<')));
}

void TestTargetProfile::legacyTlsFlagReconcilesWithTheNewPolicy() {
    S3Config config;

    // A connection saved by the original app has only `usessl`.
    config.tls = TlsPolicy::Disabled;
    config.useSsl = true;
    QVERIFY(config.effectiveUseSsl());

    config.useSsl = false;
    config.tls = TlsPolicy::Disabled;
    QVERIFY(!config.effectiveUseSsl());

    // The new three-way policy implies TLS even when the legacy flag is off.
    config.tls = TlsPolicy::VerifyStrict;
    QVERIFY(config.effectiveUseSsl());
    config.tls = TlsPolicy::AllowSelfSigned;
    QVERIFY(config.effectiveUseSsl());
}

QTEST_APPLESS_MAIN(TestTargetProfile)
#include "test_target_profile.moc"
