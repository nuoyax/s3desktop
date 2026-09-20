#include "core/SigV4.h"

#include <QTest>
#include <QTimeZone>

using namespace us3;

/// Signature Version 4 against AWS's published worked examples.
///
/// These vectors are the whole reason the transport layer is trustworthy: the
/// application talks to providers whose error messages for a bad signature are
/// indistinguishable from a bad credential, so the signer has to be verified
/// against a known-good answer rather than against a live bucket.
class TestSigV4 : public QObject {
    Q_OBJECT

private slots:
    /// The "GET vanilla" example: no query, minimal headers.
    void awsGetVanilla();
    /// The "GET with a Range header" example, which exercises signed header
    /// sorting and a non-trivial canonical header block.
    void awsGetWithHeader();
    /// The "GET with a query" example: exercises canonical query encoding.
    void awsGetWithQuery();
    /// The "PUT with a body" example: exercises the payload hash.
    void awsPutWithBody();

    void uriEncoding();
    void sha256OfKnownInput();
    void hmacOfKnownInput();
    void presignIncludesRequiredParameters();
    void presignSignatureIsStableForFixedTime();
    void emptyPayloadUsesEmptyStringHash();
};

namespace {

/// The credentials from the AWS example documentation and AWS's own sigv4 test
/// suite. Both sets are published deliberately and correspond to no real account.
///
/// The two differ by one character in the secret ('/' versus '+'), and their
/// expected signatures are not interchangeable — mixing them up produces a
/// mismatch that looks like a bug in the signer. They are kept as named
/// constants so neither can be used by accident.
const QString kAccessKey = QStringLiteral("AKIAIOSFODNN7EXAMPLE");
const QString kSecretKey = QStringLiteral("wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY");
const QString kRegion = QStringLiteral("us-east-1");

const QString kSuiteAccessKey = QStringLiteral("AKIDEXAMPLE");
const QString kSuiteSecretKey = QStringLiteral("wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY");

QDateTime exampleTime() {
    // 2013-05-24T00:00:00Z
    QDateTime when(QDate(2013, 5, 24), QTime(0, 0, 0), QTimeZone::utc());
    return when;
}

} // namespace

void TestSigV4::awsGetVanilla() {
    // The "get-vanilla" case from AWS's own sigv4 test suite. It uses the
    // suite's own credentials and host, not the S3 documentation example, and
    // the two must not be mixed: the expected signature below belongs to these
    // exact inputs.
    SigV4::Request req;
    req.method = QStringLiteral("GET");
    req.host = QStringLiteral("example.amazonaws.com");
    req.path = QStringLiteral("/");
    req.canonicalQuery = QByteArray();
    req.payloadHash = SigV4::sha256Hex(QByteArray());
    req.headers = {
        {QStringLiteral("host"), req.host},
        {QStringLiteral("x-amz-date"), QStringLiteral("20150830T123600Z")},
    };

    const QDateTime when(QDate(2015, 8, 30), QTime(12, 36, 0), QTimeZone::utc());
    const SigV4::Signed signed_ =
        SigV4::sign(req, kSuiteAccessKey, kSuiteSecretKey, QStringLiteral("us-east-1"),
                    QStringLiteral("service"), when);

    // Comparing the whole header rather than just the signature substring:
    // an Authorization value with the wrong separators is rejected exactly like
    // a wrong secret, so the framing has to be pinned down too.
    const QString expectedAuth =
        QStringLiteral("AWS4-HMAC-SHA256 "
                       "Credential=AKIDEXAMPLE/20150830/us-east-1/service/aws4_request, "
                       "SignedHeaders=host;x-amz-date, "
                       "Signature=5fa00fa31553b73ebf1942676e86291e8372ff2a2260956d9b8aae1d763fbf31");

    QCOMPARE(QString::fromLatin1(signed_.authorization), expectedAuth);
    QCOMPARE(signed_.amzDate, QByteArray("20150830T123600Z"));
    QCOMPARE(signed_.contentSha256,
             QByteArray("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

void TestSigV4::awsGetWithHeader() {
    // From the "Example: GET Object" walkthrough, which signs a Range header.
    // The signature below was reproduced independently before this test was
    // written, so a failure here means the signer changed, not the vector.
    SigV4::Request req;
    req.method = QStringLiteral("GET");
    req.host = QStringLiteral("examplebucket.s3.amazonaws.com");
    req.path = QStringLiteral("/test.txt");
    req.payloadHash = SigV4::sha256Hex(QByteArray());
    req.headers = {
        {QStringLiteral("host"), req.host},
        {QStringLiteral("range"), QStringLiteral("bytes=0-9")},
        {QStringLiteral("x-amz-content-sha256"), QString::fromLatin1(req.payloadHash)},
        {QStringLiteral("x-amz-date"), QStringLiteral("20130524T000000Z")},
    };

    const SigV4::Signed signed_ =
        SigV4::sign(req, kAccessKey, kSecretKey, kRegion, QStringLiteral("s3"), exampleTime());

    QVERIFY(signed_.authorization.contains(
        "Signature=f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41"));
    // The header names must be sorted and lowercased in the SignedHeaders list.
    QVERIFY(signed_.authorization.contains("SignedHeaders=host;range;x-amz-content-sha256;x-amz-date"));
}

void TestSigV4::awsGetWithQuery() {
    // "GET with a query": list-objects with a marker and a max-keys parameter.
    SigV4::Request req;
    req.method = QStringLiteral("GET");
    req.host = QStringLiteral("examplebucket.s3.amazonaws.com");
    req.path = QStringLiteral("/");
    req.canonicalQuery = QByteArray("max-keys=2&prefix=J");
    req.payloadHash = SigV4::sha256Hex(QByteArray());
    req.headers = {
        {QStringLiteral("host"), req.host},
        {QStringLiteral("x-amz-content-sha256"), QString::fromLatin1(req.payloadHash)},
        {QStringLiteral("x-amz-date"), QStringLiteral("20130524T000000Z")},
    };

    const SigV4::Signed signed_ =
        SigV4::sign(req, kAccessKey, kSecretKey, kRegion, QStringLiteral("s3"), exampleTime());

    QVERIFY(signed_.authorization.contains(
        "Signature=34b48302e7b5fa45bde8084f4b7868a86f0a534bc59db6670ed5711ef69dc6f7"));
}

void TestSigV4::awsPutWithBody() {
    const QByteArray body = "Welcome to Amazon S3.";
    SigV4::Request req;
    req.method = QStringLiteral("PUT");
    req.host = QStringLiteral("examplebucket.s3.amazonaws.com");
    req.path = QStringLiteral("/test%24file.text");
    req.canonicalQuery = QByteArray();
    req.payloadHash = SigV4::sha256Hex(body);
    req.headers = {
        {QStringLiteral("date"), QStringLiteral("Fri, 24 May 2013 00:00:00 GMT")},
        {QStringLiteral("host"), req.host},
        {QStringLiteral("x-amz-content-sha256"), QString::fromLatin1(req.payloadHash)},
        {QStringLiteral("x-amz-date"), QStringLiteral("20130524T000000Z")},
        {QStringLiteral("x-amz-storage-class"), QStringLiteral("REDUCED_REDUNDANCY")},
    };

    const SigV4::Signed signed_ =
        SigV4::sign(req, kAccessKey, kSecretKey, kRegion, QStringLiteral("s3"), exampleTime());

    QCOMPARE(signed_.contentSha256,
             QByteArray("44ce7dd67c959e0d3524ffac1771dfbba87d2b6b4b4e99e42034a8b803f8b072"));
    QVERIFY(signed_.authorization.contains(
        "Signature=98ad721746da40c64f1a55b78f14c238d841ea1380cd77a1b5971af0ece108bd"));
}

void TestSigV4::uriEncoding() {
    // The AWS rule: unreserved characters are A-Z a-z 0-9 - _ . ~ ; everything
    // else is percent-encoded in uppercase hex.
    QCOMPARE(SigV4::uriEncode("abcXYZ019", false), QByteArray("abcXYZ019"));
    QCOMPARE(SigV4::uriEncode("-_.~", false), QByteArray("-_.~"));

    // A slash survives in a path but not in a query value.
    QCOMPARE(SigV4::uriEncode("a/b", false), QByteArray("a/b"));
    QCOMPARE(SigV4::uriEncode("a/b", true), QByteArray("a%2Fb"));

    // A space is %20, never '+': a plus would be re-encoded by the server and
    // the signature would not match.
    QCOMPARE(SigV4::uriEncode("a b", true), QByteArray("a%20b"));
    QCOMPARE(SigV4::uriEncode("+", true), QByteArray("%2B"));

    // Non-ASCII is UTF-8 encoded first, then escaped byte by byte.
    QCOMPARE(SigV4::uriEncode(QString::fromUtf8("é").toUtf8(), true), QByteArray("%C3%A9"));

    // Uppercase hex, which is what S3 expects.
    QCOMPARE(SigV4::uriEncode("~|", true), QByteArray("~%7C"));
}

void TestSigV4::sha256OfKnownInput() {
    QCOMPARE(SigV4::sha256Hex(QByteArray("abc")),
             QByteArray("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    QCOMPARE(SigV4::sha256Hex(QByteArray()),
             QByteArray("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

void TestSigV4::hmacOfKnownInput() {
    // RFC 4231 test case 1.
    const QByteArray key = QByteArray::fromHex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    QCOMPARE(SigV4::hmacSha256(key, "Hi There").toHex(),
             QByteArray("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
}

void TestSigV4::presignIncludesRequiredParameters() {
    SigV4::Request req;
    req.method = QStringLiteral("GET");
    req.host = QStringLiteral("examplebucket.s3.amazonaws.com");
    req.path = QStringLiteral("/test.txt");

    const QString url = SigV4::presignGet(req, kAccessKey, kSecretKey, kRegion,
                                          QStringLiteral("s3"), exampleTime(), 3600, true);

    QVERIFY(url.startsWith(QStringLiteral("https://examplebucket.s3.amazonaws.com/test.txt?")));
    QVERIFY(url.contains(QStringLiteral("X-Amz-Algorithm=AWS4-HMAC-SHA256")));
    QVERIFY(url.contains(QStringLiteral("X-Amz-Expires=3600")));
    QVERIFY(url.contains(QStringLiteral("X-Amz-Date=20130524T000000Z")));
    QVERIFY(url.contains(QStringLiteral(
        "X-Amz-Credential=AKIAIOSFODNN7EXAMPLE%2F20130524%2Fus-east-1%2Fs3%2Faws4_request")));
    QVERIFY(url.contains(QStringLiteral("X-Amz-SignedHeaders=host")));
    QVERIFY(url.contains(QStringLiteral("X-Amz-Signature=")));

    // A presigned URL carries UNSIGNED-PAYLOAD, not the empty-body hash: the
    // signature is computed once, before the body is known.
    QVERIFY(!url.contains(QStringLiteral("e3b0c44298fc1c149afbf4c8996fb924")));
}

void TestSigV4::presignSignatureIsStableForFixedTime() {
    // Same inputs, same signature. This is the property that makes a presigned
    // URL shareable: the recipient does not need to re-run the signer.
    auto presign = []() {
        SigV4::Request req;
        req.method = QStringLiteral("GET");
        req.host = QStringLiteral("s3-cn-bj.ufileos.com");
        req.path = QStringLiteral("/my-bucket/photos/cat.jpg");
        return SigV4::presignGet(req, kAccessKey, kSecretKey, QStringLiteral("cn-bj"),
                                 QStringLiteral("s3"), exampleTime(), 3600, true);
    };

    QCOMPARE(presign(), presign());

    // And a different region must yield a different signature, since the region
    // is part of the credential scope.
    SigV4::Request req;
    req.method = QStringLiteral("GET");
    req.host = QStringLiteral("s3-cn-bj.ufileos.com");
    req.path = QStringLiteral("/my-bucket/photos/cat.jpg");
    const QString other = SigV4::presignGet(req, kAccessKey, kSecretKey,
                                            QStringLiteral("cn-sh2"), QStringLiteral("s3"),
                                            exampleTime(), 3600, true);

    QVERIFY(other != presign());
}

void TestSigV4::emptyPayloadUsesEmptyStringHash() {
    // An empty file has a real, well-known SHA256 — not UNSIGNED-PAYLOAD. Using
    // the latter for a buffered request is accepted by AWS but rejected by some
    // S3-compatible servers, so the distinction is worth pinning down.
    QCOMPARE(SigV4::sha256Hex(QByteArray()),
             QByteArray("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    QCOMPARE(QByteArray(SigV4::kUnsignedPayload), QByteArray("UNSIGNED-PAYLOAD"));
}

QTEST_APPLESS_MAIN(TestSigV4)
#include "test_sigv4.moc"
