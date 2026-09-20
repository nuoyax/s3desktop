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

    // The URL a connection actually produces. Getting this wrong is invisible
    // until a request fails with an error that names something else entirely.
    void hostKeepsAPort();
    void hostOmitsTheDefaultPort();
    void hostStripsASchemeBeforeBuildingTheUrl();
    void hostStripsAPathFromAPastedEndpoint();
    void hostRejectsANonNumericPort();
    void hostHandlesAnIpv6Literal();
    void anEndpointSchemeBeatsTheTlsCheckbox();
    void hostHandlesAnIpv6LiteralUnbracketed();
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

// ---------------------------------------------------------------------------
// host() and port()
//
// These do double duty: host() is the value signed as the Host header, and
// host()+port() is the authority of the URL. A mismatch between the two is a
// SignatureDoesNotMatch that the server reports without explanation, so both
// halves are pinned down here.
// ---------------------------------------------------------------------------

void TestTargetProfile::hostKeepsAPort() {
    S3Config config;
    config.endpoint = QStringLiteral("10.0.0.5:9000");
    config.useSsl = false;

    // Both halves are needed and neither implies the other: the Host header must
    // name the port the service is actually listening on, and region derivation
    // must not see it.
    QCOMPARE(config.host(), QStringLiteral("10.0.0.5"));
    QCOMPARE(config.port(), QStringLiteral("9000"));
}

void TestTargetProfile::hostOmitsTheDefaultPort() {
    S3Config config;
    config.useSsl = true;

    config.endpoint = QStringLiteral("s3-cn-bj.ufileos.com:443");
    QCOMPARE(config.host(), QStringLiteral("s3-cn-bj.ufileos.com"));
    QCOMPARE(config.port(), QStringLiteral("443"));

    // A non-default port is kept: without it the URL reaches nothing.
    config.endpoint = QStringLiteral("s3-cn-bj.ufileos.com:8443");
    QCOMPARE(config.port(), QStringLiteral("8443"));
}

void TestTargetProfile::hostStripsASchemeBeforeBuildingTheUrl() {
    // The bug this guards: the endpoint is stored exactly as typed, and a user
    // who pastes "http://203.0.113.10:3900" into the endpoint field got a URL
    // of "https://http://203.0.113.10:3900/". QUrl accepts that, reads "http"
    // as the host, and the request fails with "Host http not found".
    S3Config config;
    config.endpoint = QStringLiteral("http://203.0.113.10:3900");
    config.useSsl = true;

    QCOMPARE(config.host(), QStringLiteral("203.0.113.10"));
    QCOMPARE(config.port(), QStringLiteral("3900"));

    // No part of the scheme survives into either half.
    QVERIFY(!config.host().contains(QStringLiteral("://")));
    QVERIFY(!config.host().contains(QStringLiteral("http")));
}

void TestTargetProfile::hostStripsAPathFromAPastedEndpoint() {
    S3Config config;
    config.endpoint = QStringLiteral("https://s3-cn-bj.ufileos.com:9000/some/bucket/path");
    config.useSsl = true;

    QCOMPARE(config.host(), QStringLiteral("s3-cn-bj.ufileos.com"));
    QCOMPARE(config.port(), QStringLiteral("9000"));
}

void TestTargetProfile::hostRejectsANonNumericPort() {
    // A colon that is not a port — a stray paste, or a hostname with a colon in
    // it — must not produce a port string that gets spliced into a URL.
    S3Config config;
    config.endpoint = QStringLiteral("s3.example.com:notaport");

    QCOMPARE(config.host(), QStringLiteral("s3.example.com"));
    QVERIFY(config.port().isEmpty());

    config.endpoint = QStringLiteral("s3.example.com:");
    QVERIFY(config.port().isEmpty());
}

void TestTargetProfile::hostHandlesAnIpv6Literal() {
    S3Config config;
    config.endpoint = QStringLiteral("[2001:db8::1]:9000");
    config.useSsl = false;

    // The brackets stay: a bare "2001:db8::1" is not a valid URL authority and
    // the colons inside it are not port separators.
    QCOMPARE(config.host(), QStringLiteral("[2001:db8::1]"));
    QCOMPARE(config.port(), QStringLiteral("9000"));

    config.endpoint = QStringLiteral("[2001:db8::1]");
    QCOMPARE(config.host(), QStringLiteral("[2001:db8::1]"));
    QVERIFY(config.port().isEmpty());
}

void TestTargetProfile::anEndpointSchemeBeatsTheTlsCheckbox() {
    // The case this exists for: a connection saved with the "Use SSL" box ticked
    // and an endpoint the user pasted complete with "http://". Sending https to a
    // plain-HTTP service fails as a TLS handshake error, which points at
    // certificates rather than at the two fields that disagree.
    S3Config config;
    config.useSsl = true;
    config.tls = TlsPolicy::VerifyStrict;
    config.endpoint = QStringLiteral("http://203.0.113.10:3900");

    QCOMPARE(config.schemeFromEndpoint(), QStringLiteral("http"));
    QVERIFY(!config.effectiveUseSsl());

    // And the reverse: an https endpoint with the box cleared still gets TLS.
    config.useSsl = false;
    config.tls = TlsPolicy::Disabled;
    config.endpoint = QStringLiteral("https://s3-cn-bj.ufileos.com");
    QCOMPARE(config.schemeFromEndpoint(), QStringLiteral("https"));
    QVERIFY(config.effectiveUseSsl());

    // No scheme means the checkbox and the policy decide, as before.
    config.endpoint = QStringLiteral("s3-cn-bj.ufileos.com");
    QVERIFY(config.schemeFromEndpoint().isEmpty());
    QVERIFY(!config.effectiveUseSsl());
    config.useSsl = true;
    QVERIFY(config.effectiveUseSsl());

    // A scheme we cannot speak is not a transport decision.
    config.endpoint = QStringLiteral("ftp://s3.example.com");
    QVERIFY(config.schemeFromEndpoint().isEmpty());
}

void TestTargetProfile::hostHandlesAnIpv6LiteralUnbracketed() {
    // A literal pasted without brackets cannot be parsed: every colon in it looks
    // like a port separator. Truncating silently would send requests to a host
    // named "2001", so validate() rejects it instead and says why.
    S3Config config;
    config.name = QStringLiteral("v6");
    config.accessKey = QStringLiteral("key");
    config.secretKey = QStringLiteral("secret");
    config.endpoint = QStringLiteral("2001:db8::1:9000");

    QVERIFY(config.validate().contains(QStringLiteral("brackets")));

    // Bracketed, the same address parses cleanly.
    config.endpoint = QStringLiteral("[2001:db8::1]:9000");
    QVERIFY(config.validate().isEmpty());
    QCOMPARE(config.host(), QStringLiteral("[2001:db8::1]"));
    QCOMPARE(config.port(), QStringLiteral("9000"));
}

QTEST_APPLESS_MAIN(TestTargetProfile)
#include "test_target_profile.moc"
