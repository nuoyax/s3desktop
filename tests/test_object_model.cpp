#include "compat/TargetProfile.h"
#include "core/S3Error.h"
#include "core/S3Types.h"
#include "ui/ObjectModel.h"

#include <QDateTime>
#include <QTest>
#include <QTimeZone>

using namespace s3desktop;

/// The object table's filtering, folder synthesis and sorting.
///
/// The original's equivalent logic was a switch with fallthrough cases inside
/// the window class and was reachable only by typing into the UI. Sorting in
/// particular was advertised by an icon and implemented nowhere.
class TestObjectModel : public QObject {
    Q_OBJECT

private slots:
    void showsEveryObjectWithNoPrefixOrSearch();
    void filtersByPrefix();
    void searchReachesIntoSubPrefixes();
    void searchIsCaseInsensitive();
    void synthesisesFoldersFromKeys();
    void hidesFoldersWhileSearching();
    void foldersCanBeDisabled();
    void sortsByNameAscendingAndDescending();
    void sortsBySizeNumerically();
    void sortsByDate();
    void visibleObjectsExcludeFolders();
    void displayNameDropsTheSharedPrefix();
    void appendGrowsTheListWithoutDuplicating();
    void clearEmptiesEverything();
    void folderRowsCarryAPrefixKey();

    // --- bucket mode ---
    void bucketRowsListTheAccountBuckets();
    void bucketRowsSortByNameAndReverse();
    void bucketRowsSearchByName();
    void bucketRowsCarryTheirBucketAndNoKey();
    void bucketRowsHideTheSizeColumnHeading();
    void bucketRowsAreDroppedByClear();
    void setObjectsLeavesBucketMode();
};

namespace {

ObjectInfo makeObject(const QString &key, qint64 size, const QDateTime &when) {
    ObjectInfo object;
    object.key = key;
    object.size = size;
    object.lastModified = when;
    object.etag = QStringLiteral("\"etag\"");
    return object;
}

QList<ObjectInfo> sampleObjects() {
    const QDateTime base(QDate(2024, 1, 1), QTime(12, 0, 0), QTimeZone::utc());
    return {
        makeObject(QStringLiteral("photos/a.jpg"), 300, base.addDays(3)),
        makeObject(QStringLiteral("photos/b.jpg"), 100, base.addDays(1)),
        makeObject(QStringLiteral("photos/c.jpg"), 200, base.addDays(2)),
        makeObject(QStringLiteral("docs/notes.txt"), 50, base.addDays(9)),
        makeObject(QStringLiteral("readme.md"), 10, base.addDays(5)),
    };
}

QStringList keysOf(const ObjectModel &model) {
    QStringList keys;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row, ObjectModel::NameColumn);
        if (!index.data(ObjectModel::IsFolderRole).toBool() &&
            !index.data(ObjectModel::IsBucketRole).toBool()) {
            keys.append(index.data(ObjectModel::KeyRole).toString());
        }
    }
    return keys;
}

QList<BucketInfo> sampleBuckets() {
    const QDateTime base(QDate(2024, 1, 1), QTime(12, 0, 0), QTimeZone::utc());
    BucketInfo photos;
    photos.name = QStringLiteral("photos");
    photos.creationDate = base.addDays(3);
    BucketInfo archive;
    archive.name = QStringLiteral("archive");
    archive.creationDate = base.addDays(1);
    BucketInfo logs;
    logs.name = QStringLiteral("logs");
    logs.creationDate = base.addDays(2);
    return {photos, archive, logs};
}

} // namespace

// ---------------------------------------------------------------------------
// Bucket mode: the root level of a connection that names no bucket.
// ---------------------------------------------------------------------------

void TestObjectModel::bucketRowsListTheAccountBuckets() {
    ObjectModel model;
    model.setBuckets(sampleBuckets());

    QVERIFY(model.showingBuckets());
    QCOMPARE(model.visibleCount(), 3);
    // No objects, and so no folder rows either: this is a different list.
    QCOMPARE(model.objectCount(), 0);
    QCOMPARE(model.folderCount(), 0);
}

void TestObjectModel::bucketRowsSortByNameAndReverse() {
    ObjectModel model;
    model.setBuckets(sampleBuckets());

    QStringList names;
    for (int row = 0; row < model.rowCount(); ++row) {
        names.append(model.index(row, ObjectModel::NameColumn).data(Qt::DisplayRole).toString());
    }
    QCOMPARE(names, QStringList({QStringLiteral("archive"), QStringLiteral("logs"),
                                 QStringLiteral("photos")}));

    model.sort(ObjectModel::NameColumn, Qt::DescendingOrder);

    names.clear();
    for (int row = 0; row < model.rowCount(); ++row) {
        names.append(model.index(row, ObjectModel::NameColumn).data(Qt::DisplayRole).toString());
    }
    QCOMPARE(names, QStringList({QStringLiteral("photos"), QStringLiteral("logs"),
                                 QStringLiteral("archive")}));
}

void TestObjectModel::bucketRowsSearchByName() {
    ObjectModel model;
    model.setBuckets(sampleBuckets());
    model.setSearchText(QStringLiteral("OG"));

    QCOMPARE(model.visibleCount(), 1);
    QCOMPARE(model.index(0, ObjectModel::NameColumn).data(ObjectModel::FolderNameRole).toString(),
             QStringLiteral("logs"));
}

void TestObjectModel::bucketRowsCarryTheirBucketAndNoKey() {
    ObjectModel model;
    model.setBuckets(sampleBuckets());

    const QModelIndex index = model.index(0, ObjectModel::NameColumn);
    QVERIFY(index.data(ObjectModel::IsBucketRole).toBool());
    QVERIFY(!index.data(ObjectModel::IsFolderRole).toBool());

    BucketInfo bucket;
    QVERIFY(model.bucketAt(index, &bucket));
    QCOMPARE(bucket.name, QStringLiteral("archive"));

    ObjectInfo object;
    QVERIFY(!model.objectAt(index, &object));

    // A bucket is not a prefix: a selected bucket must not look like the
    // selected object "archive/".
    QVERIFY(index.data(ObjectModel::KeyRole).toString().isEmpty());
}

void TestObjectModel::bucketRowsHideTheSizeColumnHeading() {
    ObjectModel model;
    model.setBuckets(sampleBuckets());

    QCOMPARE(model.headerData(ObjectModel::NameColumn, Qt::Horizontal, Qt::DisplayRole).toString(),
             QStringLiteral("Bucket"));
    QVERIFY(model.headerData(ObjectModel::SizeColumn, Qt::Horizontal, Qt::DisplayRole)
                .toString()
                .isEmpty());
    QCOMPARE(
        model.headerData(ObjectModel::ModifiedColumn, Qt::Horizontal, Qt::DisplayRole).toString(),
        QStringLiteral("Created"));
}

void TestObjectModel::bucketRowsAreDroppedByClear() {
    ObjectModel model;
    model.setBuckets(sampleBuckets());
    QCOMPARE(model.visibleCount(), 3);

    model.clear();

    QVERIFY(!model.showingBuckets());
    QCOMPARE(model.visibleCount(), 0);
    QCOMPARE(model.objectCount(), 0);
}

void TestObjectModel::setObjectsLeavesBucketMode() {
    ObjectModel model;
    model.setBuckets(sampleBuckets());
    QVERIFY(model.showingBuckets());

    model.clear();
    model.setObjects(sampleObjects());

    QVERIFY(!model.showingBuckets());
    QCOMPARE(model.objectCount(), 5);
}

void TestObjectModel::showsEveryObjectWithNoPrefixOrSearch() {
    ObjectModel model;
    model.setObjects(sampleObjects());

    QCOMPARE(model.objectCount(), 5);
    // Two folders are synthesised (photos, docs) on top of the five objects.
    QCOMPARE(model.visibleCount(), 7);
    QCOMPARE(model.folderCount(), 2);
}

void TestObjectModel::filtersByPrefix() {
    ObjectModel model;
    model.setObjects(sampleObjects());
    model.setPrefix(QStringLiteral("photos/"));

    QCOMPARE(model.objectCount(), 5); // the raw list is never pruned
    QCOMPARE(keysOf(model), QStringList({QStringLiteral("photos/a.jpg"),
                                         QStringLiteral("photos/b.jpg"),
                                         QStringLiteral("photos/c.jpg")}));
    QCOMPARE(model.folderCount(), 0);
}

void TestObjectModel::searchReachesIntoSubPrefixes() {
    ObjectModel model;
    model.setObjects(sampleObjects());
    // Standing at the root, a search must find keys below prefixes the user
    // never navigated into.
    model.setSearchText(QStringLiteral("notes"));

    QCOMPARE(keysOf(model), QStringList({QStringLiteral("docs/notes.txt")}));

    model.setSearchText(QStringLiteral(".jpg"));
    QCOMPARE(keysOf(model).size(), 3);
}

void TestObjectModel::searchIsCaseInsensitive() {
    ObjectModel model;
    model.setObjects(sampleObjects());

    model.setSearchText(QStringLiteral("README"));
    QCOMPARE(keysOf(model), QStringList({QStringLiteral("readme.md")}));

    model.setSearchText(QStringLiteral("readme"));
    QCOMPARE(keysOf(model), QStringList({QStringLiteral("readme.md")}));
}

void TestObjectModel::synthesisesFoldersFromKeys() {
    ObjectModel model;
    model.setObjects(sampleObjects());

    QStringList folders;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row, ObjectModel::NameColumn);
        if (index.data(ObjectModel::IsFolderRole).toBool()) {
            folders.append(index.data(ObjectModel::FolderNameRole).toString());
        }
    }

    QCOMPARE(folders.size(), 2);
    QVERIFY(folders.contains(QStringLiteral("photos")));
    QVERIFY(folders.contains(QStringLiteral("docs")));

    // Folders come first, so navigation is always at the top of the table.
    QVERIFY(model.index(0, 0).data(ObjectModel::IsFolderRole).toBool());
}

void TestObjectModel::hidesFoldersWhileSearching() {
    ObjectModel model;
    model.setObjects(sampleObjects());
    model.setSearchText(QStringLiteral("jpg"));

    // A result set is a flat list of matches; showing "photos" above them would
    // imply the folder itself matched the search.
    QCOMPARE(model.folderCount(), 0);
    QCOMPARE(model.visibleCount(), 3);
}

void TestObjectModel::foldersCanBeDisabled() {
    ObjectModel model;
    model.setObjects(sampleObjects());
    model.setFoldersEnabled(false);

    QCOMPARE(model.folderCount(), 0);
    QCOMPARE(model.visibleCount(), 5);
}

void TestObjectModel::sortsByNameAscendingAndDescending() {
    ObjectModel model;
    model.setObjects(sampleObjects());
    model.setFoldersEnabled(false);

    model.sort(ObjectModel::NameColumn, Qt::AscendingOrder);
    QCOMPARE(keysOf(model), QStringList({QStringLiteral("docs/notes.txt"),
                                         QStringLiteral("photos/a.jpg"),
                                         QStringLiteral("photos/b.jpg"),
                                         QStringLiteral("photos/c.jpg"),
                                         QStringLiteral("readme.md")}));

    model.sort(ObjectModel::NameColumn, Qt::DescendingOrder);
    QCOMPARE(keysOf(model).first(), QStringLiteral("readme.md"));
    QCOMPARE(keysOf(model).last(), QStringLiteral("docs/notes.txt"));
}

void TestObjectModel::sortsBySizeNumerically() {
    ObjectModel model;
    model.setObjects(sampleObjects());
    model.setFoldersEnabled(false);

    model.sort(ObjectModel::SizeColumn, Qt::AscendingOrder);
    // Sizes are compared as numbers: a string comparison would put 100 before
    // 50, which is the classic object-browser bug.
    const QStringList ascending = keysOf(model);
    QCOMPARE(ascending.first(), QStringLiteral("readme.md")); // 10
    QCOMPARE(ascending.last(), QStringLiteral("photos/a.jpg")); // 300

    model.sort(ObjectModel::SizeColumn, Qt::DescendingOrder);
    QCOMPARE(keysOf(model).first(), QStringLiteral("photos/a.jpg"));
}

void TestObjectModel::sortsByDate() {
    ObjectModel model;
    model.setObjects(sampleObjects());
    model.setFoldersEnabled(false);

    model.sort(ObjectModel::ModifiedColumn, Qt::DescendingOrder);
    // docs/notes.txt is nine days after the base date, the newest object.
    QCOMPARE(keysOf(model).first(), QStringLiteral("docs/notes.txt"));

    model.sort(ObjectModel::ModifiedColumn, Qt::AscendingOrder);
    QCOMPARE(keysOf(model).first(), QStringLiteral("photos/b.jpg"));
}

void TestObjectModel::visibleObjectsExcludeFolders() {
    ObjectModel model;
    model.setObjects(sampleObjects());

    // Bulk delete and "select all" act on this list, so a folder appearing here
    // would mean trying to delete a key that does not exist.
    const QList<ObjectInfo> visible = model.visibleObjects();
    QCOMPARE(visible.size(), 5);
    for (const ObjectInfo &object : visible) {
        QVERIFY(!object.key.isEmpty());
    }
}

void TestObjectModel::displayNameDropsTheSharedPrefix() {
    ObjectModel model;
    model.setObjects(sampleObjects());
    model.setPrefix(QStringLiteral("photos/"));

    // The breadcrumb already says "photos", so repeating it on every row wastes
    // the widest column.
    const QModelIndex first = model.index(0, ObjectModel::NameColumn);
    QVERIFY(first.data(Qt::DisplayRole).toString().startsWith(QStringLiteral("a.jpg")));
    // The full key is still available, and it is what a download needs.
    QCOMPARE(first.data(ObjectModel::KeyRole).toString(), QStringLiteral("photos/a.jpg"));
}

void TestObjectModel::appendGrowsTheListWithoutDuplicating() {
    ObjectModel model;
    QList<ObjectInfo> firstPage = sampleObjects().mid(0, 2);
    model.setObjects(firstPage);
    QCOMPARE(model.objectCount(), 2);

    const QDateTime base(QDate(2024, 2, 1), QTime(9, 0, 0), QTimeZone::utc());
    model.appendObjects({makeObject(QStringLiteral("later/one.bin"), 1, base),
                         makeObject(QStringLiteral("later/two.bin"), 2, base)});

    QCOMPARE(model.objectCount(), 4);
    QCOMPARE(model.visibleCount(), 4 + model.folderCount());
}

void TestObjectModel::clearEmptiesEverything() {
    ObjectModel model;
    model.setObjects(sampleObjects());
    model.clear();

    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.objectCount(), 0);
    QCOMPARE(model.folderCount(), 0);
    QVERIFY(model.visibleObjects().isEmpty());
}

void TestObjectModel::folderRowsCarryAPrefixKey() {
    ObjectModel model;
    model.setObjects(sampleObjects());

    int checked = 0;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row, ObjectModel::NameColumn);
        if (!index.data(ObjectModel::IsFolderRole).toBool()) {
            continue;
        }
        // The row's key must be the prefix to navigate to, with a trailing
        // slash, since that is what the server expects in `prefix=`.
        const QString key = index.data(ObjectModel::KeyRole).toString();
        QVERIFY(key.endsWith(QLatin1Char('/')));
        QVERIFY(key == QStringLiteral("photos/") || key == QStringLiteral("docs/"));

        // A folder row has no meaningful size or date, and must not report one.
        QVERIFY(!index.data(ObjectModel::SizeRole).isValid());
        QVERIFY(model.index(row, ObjectModel::SizeColumn)
                    .data(ObjectModel::RawIndexRole)
                    .toInt() == -1);
        ++checked;
    }
    QCOMPARE(checked, 2);
}

QTEST_MAIN(TestObjectModel)
#include "test_object_model.moc"
