#include "core/S3Client.h"
#include "core/S3Error.h"

#include <QTest>

using namespace us3;

/// Response parsing, exercised against captured response bodies.
///
/// The original app delegated XML handling to minio-go and never tested it, so a
/// provider whose response differed in a small way produced a silent empty list
/// rather than an error. These cases pin down the shapes that actually occur.
class TestListParser : public QObject {
    Q_OBJECT

private slots:
    void parsesAMinimalObjectList();
    void readsTruncationAndResumeKey();
    void ignoresAnEmptyList();
    void handlesAMissingContentsElement();
    void toleratesAnUnknownProviderExtension();
    void readsStorageClassAndEtag();
    void rejectsNonXml();
    void parsesABucketList();

    void mapsAServerErrorCode();
    void mapsASignatureFailureToAuth();
    void mapsATooSkewedClockToAuth();
    void degradesAnUnparseableErrorBody();
    void mapsHttpStatusWhenNoCodeIsPresent();
    void marksServerErrorsRetryable();
    void doesNotRetryAccessDenied();
};

void TestListParser::parsesAMinimalObjectList() {
    const QByteArray xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<ListBucketResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Name>photos</Name>
  <Prefix></Prefix>
  <KeyCount>2</KeyCount>
  <MaxKeys>1000</MaxKeys>
  <IsTruncated>false</IsTruncated>
  <Contents>
    <Key>cat.jpg</Key>
    <LastModified>2024-03-01T09:15:00.000Z</LastModified>
    <ETag>"d41d8cd98f00b204e9800998ecf8427e"</ETag>
    <Size>1024</Size>
    <StorageClass>STANDARD</StorageClass>
  </Contents>
  <Contents>
    <Key>dog.jpg</Key>
    <LastModified>2024-03-02T11:30:00.000Z</LastModified>
    <ETag>"0cc175b9c0f1b6a831c399e269772661"</ETag>
    <Size>2048</Size>
  </Contents>
</ListBucketResult>)";

    const FlatListResult result = S3Client::parseObjectList(xml);

    QVERIFY(result.ok());
    QCOMPARE(result.objects.size(), 2);
    QCOMPARE(result.objects.at(0).key, QStringLiteral("cat.jpg"));
    QCOMPARE(result.objects.at(0).size, qint64(1024));
    QCOMPARE(result.objects.at(0).etag, QStringLiteral("\"d41d8cd98f00b204e9800998ecf8427e\""));
    QCOMPARE(result.objects.at(0).storageClass, QStringLiteral("STANDARD"));
    QVERIFY(result.objects.at(0).lastModified.isValid());
    QCOMPARE(result.objects.at(0).lastModified.date(), QDate(2024, 3, 1));
    QCOMPARE(result.objects.at(1).key, QStringLiteral("dog.jpg"));
    // No StorageClass element: left empty rather than invented, and the model
    // displays STANDARD for it.
    QVERIFY(result.objects.at(1).storageClass.isEmpty());
}

void TestListParser::readsTruncationAndResumeKey() {
    const QByteArray xml = R"(<ListBucketResult>
  <IsTruncated>true</IsTruncated>
  <Contents><Key>a/1.txt</Key><Size>1</Size></Contents>
  <Contents><Key>a/2.txt</Key><Size>2</Size></Contents>
</ListBucketResult>)";

    const FlatListResult result = S3Client::parseObjectList(xml);

    QVERIFY(result.ok());
    QVERIFY(result.isTruncated);
    // The resume point is the last key of the page, which is what "start-after"
    // needs. Using a continuation token here would not work on every provider.
    QCOMPARE(result.nextStartAfter, QStringLiteral("a/2.txt"));
}

void TestListParser::ignoresAnEmptyList() {
    const QByteArray xml = R"(<ListBucketResult>
  <IsTruncated>false</IsTruncated>
  <KeyCount>0</KeyCount>
</ListBucketResult>)";

    const FlatListResult result = S3Client::parseObjectList(xml);

    QVERIFY(result.ok());
    QVERIFY(result.objects.isEmpty());
    QVERIFY(!result.isTruncated);
    QVERIFY(result.nextStartAfter.isEmpty());
}

void TestListParser::handlesAMissingContentsElement() {
    // A prefix with no objects: valid, and the common case when a user clicks
    // into an empty folder.
    const FlatListResult result =
        S3Client::parseObjectList(QByteArray("<ListBucketResult/>"));

    QVERIFY(result.ok());
    QVERIFY(result.objects.isEmpty());
}

void TestListParser::toleratesAnUnknownProviderExtension() {
    // UCloud and other providers add elements (here, a per-object owner block
    // and a non-standard field). Parsing must not choke on them, and the extra
    // elements must not leak into neighbouring objects.
    const QByteArray xml = R"(<ListBucketResult>
  <IsTruncated>false</IsTruncated>
  <Contents>
    <Key>report.pdf</Key>
    <Size>5300</Size>
    <Owner><ID>ufile-owner</ID><DisplayName>ops</DisplayName></Owner>
    <StorageClass>STANDARD_IA</StorageClass>
    <X-Ufile-Tier>standard</X-Ufile-Tier>
  </Contents>
  <Contents>
    <Key>notes.txt</Key>
    <Size>12</Size>
  </Contents>
</ListBucketResult>)";

    const FlatListResult result = S3Client::parseObjectList(xml);

    QVERIFY(result.ok());
    QCOMPARE(result.objects.size(), 2);
    QCOMPARE(result.objects.at(0).key, QStringLiteral("report.pdf"));
    QCOMPARE(result.objects.at(0).size, qint64(5300));
    QCOMPARE(result.objects.at(0).storageClass, QStringLiteral("STANDARD_IA"));
    QCOMPARE(result.objects.at(1).key, QStringLiteral("notes.txt"));
    // The second object must not inherit the first one's storage class.
    QVERIFY(result.objects.at(1).storageClass.isEmpty());
}

void TestListParser::readsStorageClassAndEtag() {
    const QByteArray xml = R"(<ListBucketResult>
  <Contents>
    <Key>archive/2023.tar</Key>
    <Size>1099511627776</Size>
    <ETag>"abc123"</ETag>
    <StorageClass>GLACIER</StorageClass>
  </Contents>
</ListBucketResult>)";

    const FlatListResult result = S3Client::parseObjectList(xml);

    QCOMPARE(result.objects.size(), 1);
    // A terabyte must survive the round trip through QString::toLongLong.
    QCOMPARE(result.objects.at(0).size, qint64(1099511627776LL));
    QCOMPARE(result.objects.at(0).storageClass, QStringLiteral("GLACIER"));
}

void TestListParser::rejectsNonXml() {
    // An HTML proxy error page served with a 200, which is what a misconfigured
    // reverse proxy in front of a private deployment produces.
    const FlatListResult result =
        S3Client::parseObjectList(QByteArray("<html><body>502 Bad Gateway</body></html>"));

    // The document parses as XML, so this is an empty list rather than an error.
    // What matters is that it does not crash and does not invent objects.
    QVERIFY(result.objects.isEmpty());

    // Genuinely malformed XML is reported as a protocol problem.
    const FlatListResult broken = S3Client::parseObjectList(QByteArray("<<<not xml at all"));
    QVERIFY(!broken.ok());
    QCOMPARE(broken.error.kind(), ErrorKind::Protocol);
}

void TestListParser::parsesABucketList() {
    const QByteArray xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<ListAllMyBucketsResult>
  <Owner><ID>abc</ID></Owner>
  <Buckets>
    <Bucket><Name>photos</Name><CreationDate>2023-01-04T10:00:00.000Z</CreationDate></Bucket>
    <Bucket><Name>backups</Name><CreationDate>2024-06-30T22:45:00.000Z</CreationDate></Bucket>
  </Buckets>
</ListAllMyBucketsResult>)";

    const Result<QList<BucketInfo>> result = S3Client::parseBucketList(xml);

    QVERIFY(result.ok());
    QCOMPARE(result.value.size(), 2);
    QCOMPARE(result.value.at(0).name, QStringLiteral("photos"));
    QCOMPARE(result.value.at(0).creationDate.date(), QDate(2023, 1, 4));
    QCOMPARE(result.value.at(1).name, QStringLiteral("backups"));
}

void TestListParser::mapsAServerErrorCode() {
    const QByteArray body = R"(<?xml version="1.0" encoding="UTF-8"?>
<Error>
  <Code>NoSuchBucket</Code>
  <Message>The specified bucket does not exist</Message>
  <RequestId>4442587FB7D0A2F9</RequestId>
</Error>)";

    const S3Error error = S3Error::fromResponse(404, body, QStringLiteral("header-id"));

    QCOMPARE(error.kind(), ErrorKind::NotFound);
    QCOMPARE(error.serverCode(), QStringLiteral("NoSuchBucket"));
    // The server's own wording is kept for the log, while message() stays
    // readable.
    QVERIFY(error.serverDetail().contains(QStringLiteral("does not exist")));
    QCOMPARE(error.requestId(), QStringLiteral("4442587FB7D0A2F9"));
    QVERIFY(!error.message().isEmpty());
}

void TestListParser::mapsASignatureFailureToAuth() {
    const QByteArray body = R"(<Error>
  <Code>SignatureDoesNotMatch</Code>
  <Message>The request signature we calculated does not match the signature you provided.</Message>
</Error>)";

    const S3Error error = S3Error::fromResponse(403, body);

    QCOMPARE(error.kind(), ErrorKind::Auth);
    // A signature failure is almost always a region or addressing mismatch, so
    // it must be flagged as a configuration problem for the UI to point at the
    // connection dialog rather than showing the raw server text.
    QVERIFY(error.isConfigurationProblem());
}

void TestListParser::mapsATooSkewedClockToAuth() {
    const QByteArray body =
        R"(<Error><Code>RequestTimeTooSkewed</Code><Message>clock is wrong</Message></Error>)";

    const S3Error error = S3Error::fromResponse(403, body);

    QCOMPARE(error.kind(), ErrorKind::Auth);
    // Not retryable: retrying immediately would fail identically, and the user
    // needs to fix the clock.
    QVERIFY(!error.isRetryable());
}

void TestListParser::degradesAnUnparseableErrorBody() {
    // An HTML error page from a load balancer, or an empty body. The status line
    // is all there is to go on.
    const S3Error fromHtml =
        S3Error::fromResponse(503, QByteArray("<html><body>Service Unavailable</body></html>"));
    QVERIFY(!fromHtml.isEmpty());
    QCOMPARE(fromHtml.kind(), ErrorKind::ServerError);

    const S3Error fromNothing = S3Error::fromResponse(500, QByteArray());
    QCOMPARE(fromNothing.kind(), ErrorKind::ServerError);
    QVERIFY(!fromNothing.message().isEmpty());
}

void TestListParser::mapsHttpStatusWhenNoCodeIsPresent() {
    QCOMPARE(S3Error::fromResponse(403, QByteArray()).kind(), ErrorKind::AccessDenied);
    QCOMPARE(S3Error::fromResponse(404, QByteArray()).kind(), ErrorKind::NotFound);
    QCOMPARE(S3Error::fromResponse(429, QByteArray()).kind(), ErrorKind::RateLimited);
    QCOMPARE(S3Error::fromResponse(400, QByteArray()).kind(), ErrorKind::Protocol);
    QCOMPARE(S3Error::fromResponse(504, QByteArray()).kind(), ErrorKind::Timeout);
}

void TestListParser::marksServerErrorsRetryable() {
    QVERIFY(S3Error::fromResponse(500, QByteArray()).isRetryable());
    QVERIFY(S3Error::fromResponse(503, QByteArray()).isRetryable());
    QVERIFY(S3Error::fromResponse(429, QByteArray()).isRetryable());

    const QByteArray slowDown = R"(<Error><Code>SlowDown</Code></Error>)";
    const S3Error throttled = S3Error::fromResponse(503, slowDown);
    QCOMPARE(throttled.kind(), ErrorKind::RateLimited);
    QVERIFY(throttled.isRetryable());
}

void TestListParser::doesNotRetryAccessDenied() {
    const S3Error denied = S3Error::fromResponse(403, QByteArray());
    // Retrying a permissions problem just multiplies the failures.
    QVERIFY(!denied.isRetryable());

    // Neither is a cancellation.
    QVERIFY(!S3Error::cancelled().isRetryable());

    // Nor a bad configuration, which is caught before any request goes out.
    const S3Error config = S3Error::config(QStringLiteral("An endpoint is required."));
    QCOMPARE(config.kind(), ErrorKind::Config);
    QVERIFY(!config.isRetryable());
    QVERIFY(config.isConfigurationProblem());
}

QTEST_APPLESS_MAIN(TestListParser)
#include "test_list_parser.moc"
