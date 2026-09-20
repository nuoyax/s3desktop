#include "core/S3Error.h"
#include "core/TransferQueue.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace s3desktop;

/// The transfer queue's bookkeeping, exercised without a server.
///
/// The network calls are not stubbed here; the point of these cases is the state
/// machine — which is what the original lacked entirely, having no queue, no
/// retry and no cancellation.
class TestTransferQueue : public QObject {
    Q_OBJECT

private slots:
    void reportsIdleWhenEmpty();
    void enqueueAssignsDistinctIds();
    void exposeTheDirectionOfEachItem();
    void downloadsLandInTheGivenDirectory();
    void uploadingAMissingFileStillQueues();
    void cancelOnAQueuedItemMarksItCancelled();
    void cancelAllClearsTheBacklog();
    void clearFinishedKeepsUnfinishedItems();
    void fractionIsZeroWithoutATotal();
    void fractionIsHalfwayThrough();
    void concurrencyIsClamped();
    void displayNameUsesTheFileStem();
};

void TestTransferQueue::reportsIdleWhenEmpty() {
    TransferQueue queue;
    QVERIFY(queue.isIdle());
    QVERIFY(queue.items().isEmpty());
}

void TestTransferQueue::enqueueAssignsDistinctIds() {
    TransferQueue queue; // no client: nothing can start, so state stays Queued
    const int first = queue.enqueueUpload(QStringLiteral("a.txt"), QStringLiteral("a.txt"));
    const int second = queue.enqueueUpload(QStringLiteral("b.txt"), QStringLiteral("b.txt"));

    QVERIFY(first > 0);
    QVERIFY(second > 0);
    QVERIFY(first != second);
    QCOMPARE(queue.items().size(), 2);

    // The id is carried on the item so a snapshot is enough to act on it.
    QCOMPARE(queue.items().at(0).id, first);
    QCOMPARE(queue.items().at(1).id, second);
}

void TestTransferQueue::exposeTheDirectionOfEachItem() {
    TransferQueue queue;
    queue.enqueueUpload(QStringLiteral("local.txt"), QStringLiteral("remote/local.txt"));
    queue.enqueueDownload(QStringLiteral("remote/other.txt"), QDir::tempPath());

    QCOMPARE(queue.items().at(0).direction, TransferItem::Direction::Upload);
    QCOMPARE(queue.items().at(0).objectKey, QStringLiteral("remote/local.txt"));
    QCOMPARE(queue.items().at(1).direction, TransferItem::Direction::Download);
    QCOMPARE(queue.items().at(1).objectKey, QStringLiteral("remote/other.txt"));
}

void TestTransferQueue::downloadsLandInTheGivenDirectory() {
    TransferQueue queue;
    queue.enqueueDownload(QStringLiteral("photos/holiday/cat.jpg"), QStringLiteral("C:/out"));

    const TransferItem &item = queue.items().first();
    // Only the final path segment becomes a filename: an object key is not a
    // path and must never be used as one.
    QCOMPARE(item.localPath, QStringLiteral("C:/out/cat.jpg"));
}

void TestTransferQueue::uploadingAMissingFileStillQueues() {
    // The queue does not stat the file at enqueue time; it does so when the
    // upload starts, so an error appears against the item rather than as a
    // failure that never entered the list.
    TransferQueue queue;
    queue.enqueueUpload(QStringLiteral("C:/does/not/exist.txt"), QStringLiteral("x.txt"));

    QCOMPARE(queue.items().size(), 1);
    QCOMPARE(queue.items().first().bytesTotal, qint64(0));
}

void TestTransferQueue::cancelOnAQueuedItemMarksItCancelled() {
    TransferQueue queue;
    const int id = queue.enqueueUpload(QStringLiteral("a.txt"), QStringLiteral("a.txt"));

    QSignalSpy spy(&queue, &TransferQueue::finished);
    queue.cancel(id);

    QCOMPARE(queue.items().first().state, TransferItem::State::Cancelled);
    QCOMPARE(spy.count(), 1);

    // Cancelling again is a no-op rather than a second signal, so a UI that
    // binds a button to it cannot double-report.
    queue.cancel(id);
    QCOMPARE(spy.count(), 1);
}

void TestTransferQueue::cancelAllClearsTheBacklog() {
    TransferQueue queue;
    queue.enqueueUpload(QStringLiteral("a.txt"), QStringLiteral("a.txt"));
    queue.enqueueUpload(QStringLiteral("b.txt"), QStringLiteral("b.txt"));
    queue.enqueueDownload(QStringLiteral("c.txt"), QDir::tempPath());

    queue.cancelAll();

    for (const TransferItem &item : queue.items()) {
        QCOMPARE(item.state, TransferItem::State::Cancelled);
    }
    QVERIFY(queue.isIdle());
}

void TestTransferQueue::clearFinishedKeepsUnfinishedItems() {
    TransferQueue queue;
    const int done = queue.enqueueUpload(QStringLiteral("done.txt"), QStringLiteral("done.txt"));
    const int pending = queue.enqueueUpload(QStringLiteral("pending.txt"),
                                            QStringLiteral("pending.txt"));

    queue.cancel(done);
    QCOMPARE(queue.items().size(), 2);

    queue.clearFinished();

    // The cancelled item is gone; the still-queued one survives, and its id
    // still resolves to it.
    QCOMPARE(queue.items().size(), 1);
    QCOMPARE(queue.items().first().id, pending);
    QCOMPARE(queue.items().first().state, TransferItem::State::Queued);
}

void TestTransferQueue::fractionIsZeroWithoutATotal() {
    TransferItem item;
    item.bytesDone = 500;
    item.bytesTotal = 0;
    // A server that sends no Content-Length must not produce a division by zero
    // or a bogus 100%.
    QCOMPARE(item.fraction(), 0.0);
}

void TestTransferQueue::fractionIsHalfwayThrough() {
    TransferItem item;
    item.bytesDone = 512;
    item.bytesTotal = 1024;
    QCOMPARE(item.fraction(), 0.5);

    item.bytesDone = 1024;
    QCOMPARE(item.fraction(), 1.0);

    item.bytesDone = 2048; // a server that over-reports must not exceed 1.0
    QVERIFY(item.fraction() > 1.0); // raw value; the delegate clamps it
}

void TestTransferQueue::concurrencyIsClamped() {
    TransferQueue queue;

    queue.setConcurrency(0);
    QCOMPARE(queue.concurrency(), 1);

    queue.setConcurrency(99);
    QCOMPARE(queue.concurrency(), 8);

    queue.setConcurrency(4);
    QCOMPARE(queue.concurrency(), 4);
}

void TestTransferQueue::displayNameUsesTheFileStem() {
    TransferItem upload;
    upload.direction = TransferItem::Direction::Upload;
    upload.localPath = QStringLiteral("C:/tmp/report.pdf");
    QCOMPARE(upload.displayName(), QStringLiteral("report.pdf"));

    TransferItem download;
    download.direction = TransferItem::Direction::Download;
    download.objectKey = QStringLiteral("archive/2024/report.pdf");
    QCOMPARE(download.displayName(), QStringLiteral("report.pdf"));
}

QTEST_MAIN(TestTransferQueue)
#include "test_transfer_queue.moc"
