#include "core/S3Types.h"
#include "ui/ObjectBrowser.h"
#include "ui/ObjectModel.h"

#include <QLabel>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>
#include <QToolButton>

using namespace us3;

/// Navigation, the breadcrumb, and the three nav buttons.
///
/// These are the affordances a user reaches for without reading anything: back,
/// forward, up, and clicking a crumb. Each has to mean something at every level,
/// including the level above a bucket — which is the bucket list, not a shorter
/// prefix. That is the case most likely to be wrong, so it is the case most of
/// these tests cover.
class TestObjectBrowser : public QObject {
    Q_OBJECT

private slots:
    void startsAtTheRootWithNothingToGoBackTo();
    void navigatingDownPushesHistoryAndEmitsLoad();
    void goingUpAfterGoingDownCanBeWalkedBack();
    void backAndForwardRetraceTheVisitedPrefixes();
    void navigatingToTheCurrentPrefixIsANoOp();
    void openingAFolderFromTheTableNavigatesIntoIt();

    // --- the level above a prefix ---
    void upIsDisabledWhenThereIsNowhereAbove();
    void upFromABucketRootAsksForTheBucketList();
    void upFromAPrefixGoesToTheParentNotTheBucketList();
    void bucketCrumbSwitchesToTheBucketList();

    // --- the breadcrumb ---
    void breadcrumbNamesTheBucketAndEverySegment();
    void breadcrumbOffersTheBucketListOnlyWhenItExists();
    void bucketRowsAreNotSelectableAsObjects();
};

namespace {

/// The browser never calls into the client — MainWindow decides when to load and
/// the browser decides what is on screen — so no client is needed to exercise
/// navigation.
ObjectBrowser *makeBrowser() {
    return new ObjectBrowser(nullptr);
}

/// Retired crumbs are queued for deletion, not destroyed on the spot, so they
/// are still children of the bar when a rebuild returns. Nothing else in the
/// suite spins an event loop, so the queue has to be drained by hand or the
/// previous path would still be counted.
void flushRetiredCrumbs() {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QStringList crumbLabels(const ObjectBrowser *browser) {
    flushRetiredCrumbs();

    QStringList labels;
    for (const QToolButton *button : browser->findChildren<QToolButton *>(
             QStringLiteral("crumbPart"))) {
        labels.append(button->text());
    }
    return labels;
}

QToolButton *crumbButton(const ObjectBrowser *browser, const QString &label) {
    flushRetiredCrumbs();

    for (QToolButton *button :
         browser->findChildren<QToolButton *>(QStringLiteral("crumbPart"))) {
        if (button->text() == label) {
            return button;
        }
    }
    return nullptr;
}

QToolButton *navButton(const ObjectBrowser *browser, const QString &tooltip) {
    for (QToolButton *button : browser->findChildren<QToolButton *>(
             QStringLiteral("crumbNav"))) {
        if (button->toolTip() == tooltip) {
            return button;
        }
    }
    return nullptr;
}

QToolButton *upButton(const ObjectBrowser *browser) {
    return navButton(browser, QStringLiteral("Up one level (Alt+Up)"));
}

QToolButton *backButton(const ObjectBrowser *browser) {
    return navButton(browser, QStringLiteral("Back (Alt+Left)"));
}

QToolButton *forwardButton(const ObjectBrowser *browser) {
    return navButton(browser, QStringLiteral("Forward (Alt+Right)"));
}

BucketInfo makeBucket(const QString &name) {
    BucketInfo bucket;
    bucket.name = name;
    bucket.creationDate = QDateTime(QDate(2024, 1, 1), QTime(0, 0, 0), QTimeZone::utc());
    return bucket;
}

ObjectInfo makeObject(const QString &key) {
    ObjectInfo object;
    object.key = key;
    return object;
}

} // namespace

// ---------------------------------------------------------------------------
// The baseline
// ---------------------------------------------------------------------------

void TestObjectBrowser::startsAtTheRootWithNothingToGoBackTo() {
    ObjectBrowser *browser = makeBrowser();

    QVERIFY(browser->prefix().isEmpty());
    QVERIFY(!browser->canGoBack());
    QVERIFY(!browser->canGoForward());
    // Nothing above a root with no bucket: "up" would have nowhere to land.
    QVERIFY(!browser->canGoUp());

    QVERIFY(!backButton(browser)->isEnabled());
    QVERIFY(!forwardButton(browser)->isEnabled());
    QVERIFY(!upButton(browser)->isEnabled());

    delete browser;
}

void TestObjectBrowser::navigatingDownPushesHistoryAndEmitsLoad() {
    ObjectBrowser *browser = makeBrowser();
    QSignalSpy loadSpy(browser, &ObjectBrowser::loadRequested);

    browser->navigateTo(QStringLiteral("photos/"));

    QCOMPARE(browser->prefix(), QStringLiteral("photos/"));
    QCOMPARE(loadSpy.count(), 1);
    QCOMPARE(loadSpy.first().first().toString(), QStringLiteral("photos/"));

    delete browser;
}

void TestObjectBrowser::goingUpAfterGoingDownCanBeWalkedBack() {
    ObjectBrowser *browser = makeBrowser();

    browser->navigateTo(QStringLiteral("photos/2024/"));
    browser->navigateUp();

    QCOMPARE(browser->prefix(), QStringLiteral("photos/"));
    QVERIFY(browser->canGoBack());
    QVERIFY(!browser->canGoForward());

    delete browser;
}

void TestObjectBrowser::backAndForwardRetraceTheVisitedPrefixes() {
    ObjectBrowser *browser = makeBrowser();

    browser->navigateTo(QStringLiteral("photos/"));
    browser->navigateTo(QStringLiteral("photos/2024/"));

    QVERIFY(browser->canGoBack());
    browser->goBack();
    QCOMPARE(browser->prefix(), QStringLiteral("photos/"));
    QVERIFY(browser->canGoForward());

    browser->goForward();
    QCOMPARE(browser->prefix(), QStringLiteral("photos/2024/"));

    delete browser;
}

void TestObjectBrowser::navigatingToTheCurrentPrefixIsANoOp() {
    ObjectBrowser *browser = makeBrowser();
    browser->navigateTo(QStringLiteral("photos/"));
    QSignalSpy loadSpy(browser, &ObjectBrowser::loadRequested);

    browser->navigateTo(QStringLiteral("photos/"));

    // Re-listing what is already on screen would be a wasted round trip, and it
    // would also seed the history with a duplicate.
    QCOMPARE(loadSpy.count(), 0);

    delete browser;
}

void TestObjectBrowser::openingAFolderFromTheTableNavigatesIntoIt() {
    ObjectBrowser *browser = makeBrowser();
    browser->model()->setObjects({makeObject(QStringLiteral("docs/a.txt")),
                                  makeObject(QStringLiteral("docs/b.txt"))});
    QSignalSpy loadSpy(browser, &ObjectBrowser::loadRequested);

    // Row 0 is the synthesised "docs" folder.
    const QModelIndex folder = browser->model()->index(0, ObjectModel::NameColumn);
    QVERIFY(folder.data(ObjectModel::IsFolderRole).toBool());

    browser->view()->doubleClicked(folder);

    QCOMPARE(browser->prefix(), QStringLiteral("docs/"));
    QCOMPARE(loadSpy.count(), 1);

    delete browser;
}

// ---------------------------------------------------------------------------
// The level above a prefix
// ---------------------------------------------------------------------------

void TestObjectBrowser::upIsDisabledWhenThereIsNowhereAbove() {
    ObjectBrowser *browser = makeBrowser();
    // A connection pinned to one bucket: there is no list to go up to, so the
    // bucket root is genuinely the top.
    browser->setBucketListAvailable(false);
    browser->setBucketName(QStringLiteral("my-bucket"));

    QVERIFY(!browser->canGoUp());
    QVERIFY(!upButton(browser)->isEnabled());

    delete browser;
}

void TestObjectBrowser::upFromABucketRootAsksForTheBucketList() {
    ObjectBrowser *browser = makeBrowser();
    browser->setBucketListAvailable(true);
    browser->setBucketName(QStringLiteral("my-bucket"));
    QSignalSpy bucketsSpy(browser, &ObjectBrowser::bucketsRequested);
    QSignalSpy loadSpy(browser, &ObjectBrowser::loadRequested);

    QVERIFY(browser->canGoUp());
    browser->navigateUp();

    // The bucket list is fetched by the window, not by a prefix listing: asking
    // for it as a load would send a ListObjects for the empty prefix.
    QCOMPARE(bucketsSpy.count(), 1);
    QCOMPARE(loadSpy.count(), 0);
    // And it must not pretend a prefix changed, because none did.
    QVERIFY(browser->prefix().isEmpty());

    delete browser;
}

void TestObjectBrowser::upFromAPrefixGoesToTheParentNotTheBucketList() {
    ObjectBrowser *browser = makeBrowser();
    browser->setBucketListAvailable(true);
    browser->setBucketName(QStringLiteral("my-bucket"));
    browser->navigateTo(QStringLiteral("photos/2024/"));
    QSignalSpy bucketsSpy(browser, &ObjectBrowser::bucketsRequested);
    QSignalSpy loadSpy(browser, &ObjectBrowser::loadRequested);

    browser->navigateUp();

    // One level, not two: the bucket list is another step up from here.
    QCOMPARE(browser->prefix(), QStringLiteral("photos/"));
    QCOMPARE(loadSpy.count(), 1);
    QCOMPARE(bucketsSpy.count(), 0);

    delete browser;
}

void TestObjectBrowser::bucketCrumbSwitchesToTheBucketList() {
    ObjectBrowser *browser = makeBrowser();
    browser->setBucketListAvailable(true);
    browser->setBucketName(QStringLiteral("my-bucket"));
    browser->setShowingBuckets(false);
    QSignalSpy bucketsSpy(browser, &ObjectBrowser::bucketsRequested);

    QTest::mouseClick(crumbButton(browser, QStringLiteral("Buckets")), Qt::LeftButton);

    QCOMPARE(bucketsSpy.count(), 1);
}

// ---------------------------------------------------------------------------
// The breadcrumb
// ---------------------------------------------------------------------------

void TestObjectBrowser::breadcrumbNamesTheBucketAndEverySegment() {
    ObjectBrowser *browser = makeBrowser();
    browser->setBucketListAvailable(true);
    browser->setBucketName(QStringLiteral("my-bucket"));
    browser->setShowingBuckets(false);

    // The bucket list is local, so the path is constructed from state rather
    // than from a load; the segments come from a navigation that is walked and
    // then walked back, which is also where the history is exercised.
    browser->navigateTo(QStringLiteral("photos/"));
    browser->navigateTo(QStringLiteral("photos/2024/"));
    QCOMPARE(browser->prefix(), QStringLiteral("photos/2024/"));

    browser->goBack();
    QCOMPARE(browser->prefix(), QStringLiteral("photos/"));
    QVERIFY(browser->canGoForward());

    browser->goForward();
    QCOMPARE(browser->prefix(), QStringLiteral("photos/2024/"));
    QVERIFY(!browser->canGoForward());

    browser->navigateUp();
    QCOMPARE(crumbLabels(browser),
             QStringList({QStringLiteral("Buckets"), QStringLiteral("my-bucket"),
                          QStringLiteral("photos")}));
}

void TestObjectBrowser::breadcrumbOffersTheBucketListOnlyWhenItExists() {
    ObjectBrowser *browser = makeBrowser();
    browser->setBucketListAvailable(false);
    browser->setBucketName(QStringLiteral("my-bucket"));
    browser->setShowingBuckets(false);

    // A pinned connection has no list to offer, so the path starts at the
    // bucket instead of at a "Buckets" level that would do nothing.
    QCOMPARE(crumbLabels(browser), QStringList({QStringLiteral("my-bucket")}));
}

void TestObjectBrowser::bucketRowsAreNotSelectableAsObjects() {
    ObjectBrowser *browser = makeBrowser();
    browser->setShowingBuckets(true);
    browser->model()->setBuckets({makeBucket(QStringLiteral("alpha")),
                                  makeBucket(QStringLiteral("beta"))});

    // Row 0 is a bucket. Every object command — download, delete, presigned
    // link — goes through selectedKeys(), so a bucket row must never appear
    // there, while still being navigable.
    QModelIndex row;
    for (int r = 0; r < browser->model()->rowCount(); ++r) {
        const QModelIndex candidate = browser->model()->index(r, ObjectModel::NameColumn);
        if (candidate.data(ObjectModel::FolderNameRole).toString() == QStringLiteral("alpha")) {
            row = candidate;
        }
    }
    QVERIFY(row.isValid());
    QVERIFY(row.data(ObjectModel::IsBucketRole).toBool());

    browser->view()->selectionModel()->select(
        row, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);

    QVERIFY(browser->selectedKeys().isEmpty());
    QVERIFY(browser->selectedObjects().isEmpty());

    QSignalSpy bucketSpy(browser, &ObjectBrowser::bucketActivated);
    browser->view()->doubleClicked(row);
    QCOMPARE(bucketSpy.count(), 1);
    QCOMPARE(bucketSpy.first().first().toString(), QStringLiteral("alpha"));

    delete browser;
}

QTEST_MAIN(TestObjectBrowser)
#include "test_object_browser.moc"
