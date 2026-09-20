#include "ui/ObjectModel.h"

#include "ui/Theme.h"

#include <QCollator>
#include <QLocale>

#include <algorithm>

namespace s3desktop {

ObjectModel::ObjectModel(QObject *parent) : QAbstractTableModel(parent) {}

int ObjectModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_rows.size();
}

int ObjectModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

int ObjectModel::folderCount() const {
    int n = 0;
    for (const Row &row : m_rows) {
        if (row.isFolder) {
            ++n;
        }
    }
    return n;
}

QVariant ObjectModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }

    const Row &row = m_rows.at(index.row());

    if (row.isBucket) {
        const BucketInfo &bucket = m_buckets.at(row.bucketIndex);
        switch (role) {
        case Qt::DisplayRole:
            if (index.column() == NameColumn) {
                return bucket.name;
            }
            // The creation date goes in the Modified column: it is the only
            // timestamp a bucket has, and leaving the column blank would look
            // like missing data rather than data that does not exist.
            return index.column() == ModifiedColumn && bucket.creationDate.isValid()
                       ? Theme::formatWhen(bucket.creationDate)
                       : QVariant();
        case Qt::DecorationRole:
            return index.column() == NameColumn ? Theme::icon(Theme::Glyph::Bucket) : QVariant();
        case Qt::ToolTipRole:
            return QStringLiteral("Bucket: %1").arg(bucket.name);
        case IsBucketRole:
            return true;
        case IsFolderRole:
            return false;
        case KeyRole:
            // A bucket is not a key prefix: selecting it must not look like
            // selecting the object "name/".
            return QString();
        case FolderNameRole:
            return bucket.name;
        case RawIndexRole:
        case SizeRole:
            return QVariant();
        case DateRole:
            return bucket.creationDate;
        case EtagRole:
        case StorageClassRole:
            return QString();
        case Qt::TextAlignmentRole:
            return QVariant(int(Qt::AlignLeft | Qt::AlignVCenter));
        default:
            return {};
        }
    }

    if (row.isFolder) {
        switch (role) {
        case Qt::DisplayRole:
            return index.column() == NameColumn ? row.folderName : QVariant();
        case Qt::DecorationRole:
            return index.column() == NameColumn ? Theme::icon(Theme::Glyph::Folder) : QVariant();
        case Qt::ToolTipRole:
            return index.column() == NameColumn
                       ? QStringLiteral("Folder: %1%2").arg(m_prefix, row.folderName)
                       : QVariant();
        case IsFolderRole:
            return true;
        case FolderNameRole:
            return row.folderName;
        case KeyRole:
            return m_prefix + row.folderName + QLatin1Char('/');
        case SizeRole:
            return QVariant();
        case DateRole:
            return QVariant();
        case EtagRole:
        case StorageClassRole:
            return QString();
        case RawIndexRole:
            return -1;
        case Qt::TextAlignmentRole:
            return index.column() == SizeColumn
                       ? QVariant(int(Qt::AlignRight | Qt::AlignVCenter))
                       : QVariant(int(Qt::AlignLeft | Qt::AlignVCenter));
        default:
            return {};
        }
    }

    const ObjectInfo &object = m_objects.at(row.objectIndex);

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case NameColumn: {
            // Show the part of the key below the current prefix; showing the
            // whole key would repeat the breadcrumb on every line.
            const QString shown = object.key.mid(m_prefix.size());
            return shown.isEmpty() ? object.key : shown;
        }
        case SizeColumn:
            return Theme::formatBytes(object.size);
        case ModifiedColumn:
            return Theme::formatWhen(object.lastModified);
        default:
            return {};
        }
    case Qt::DecorationRole:
        return index.column() == NameColumn ? Theme::icon(Theme::Glyph::File) : QVariant();
    case Qt::ToolTipRole:
        return object.key;
    case KeyRole:
        return object.key;
    case SizeRole:
        return object.size;
    case DateRole:
        return object.lastModified;
    case IsFolderRole:
        return false;
    case EtagRole:
        return object.etag;
    case StorageClassRole:
        return object.storageClass;
    case RawIndexRole:
        return row.objectIndex;
    case Qt::TextAlignmentRole:
        return index.column() == SizeColumn
                   ? QVariant(int(Qt::AlignRight | Qt::AlignVCenter))
                   : QVariant(int(Qt::AlignLeft | Qt::AlignVCenter));
    default:
        return {};
    }
}

QVariant ObjectModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal) {
        return {};
    }

    if (role == Qt::DisplayRole) {
        if (m_bucketMode) {
            switch (section) {
            case NameColumn:
                return QStringLiteral("Bucket");
            case SizeColumn:
                return {}; // a bucket has no size, and a blank column says so
            case ModifiedColumn:
                return QStringLiteral("Created");
            default:
                return {};
            }
        }
        switch (section) {
        case NameColumn:
            return QStringLiteral("Name");
        case SizeColumn:
            return QStringLiteral("Size");
        case ModifiedColumn:
            return QStringLiteral("Modified");
        default:
            return {};
        }
    }

    if (role == Qt::TextAlignmentRole && section == SizeColumn) {
        return int(Qt::AlignRight | Qt::AlignVCenter);
    }

    // A sort indicator instead of the original's decorative "MoveUp" icon that
    // did nothing when clicked.
    if (role == Qt::ToolTipRole) {
        return QStringLiteral("Click to sort by this column");
    }

    return {};
}

Qt::ItemFlags ObjectModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QString ObjectModel::keyAt(const QModelIndex &index) const {
    return data(index, KeyRole).toString();
}

bool ObjectModel::objectAt(const QModelIndex &index, ObjectInfo *out) const {
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return false;
    }
    const Row &row = m_rows.at(index.row());
    if (row.isFolder || row.isBucket || row.objectIndex < 0 ||
        row.objectIndex >= m_objects.size()) {
        return false;
    }
    if (out) {
        *out = m_objects.at(row.objectIndex);
    }
    return true;
}

bool ObjectModel::bucketAt(const QModelIndex &index, BucketInfo *out) const {
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return false;
    }
    const Row &row = m_rows.at(index.row());
    if (!row.isBucket || row.bucketIndex < 0 || row.bucketIndex >= m_buckets.size()) {
        return false;
    }
    if (out) {
        *out = m_buckets.at(row.bucketIndex);
    }
    return true;
}

void ObjectModel::setBuckets(const QList<BucketInfo> &buckets) {
    beginResetModel();
    m_bucketMode = true;
    m_buckets = buckets;
    rebuild();
    endResetModel();
}

QList<ObjectInfo> ObjectModel::visibleObjects() const {
    QList<ObjectInfo> result;
    result.reserve(m_rows.size());
    for (const Row &row : m_rows) {
        if (!row.isFolder && row.objectIndex >= 0 && row.objectIndex < m_objects.size()) {
            result.append(m_objects.at(row.objectIndex));
        }
    }
    return result;
}

void ObjectModel::setObjects(const QList<ObjectInfo> &objects) {
    beginResetModel();
    m_objects = objects;
    rebuild();
    endResetModel();
}

void ObjectModel::appendObjects(const QList<ObjectInfo> &objects) {
    if (objects.isEmpty()) {
        return;
    }

    m_objects.append(objects);

    // A new page can introduce a folder that belongs above rows already on
    // screen, so the visible order is recomputed rather than appended to. Only
    // the row list is rebuilt; the object list keeps growing, so paging stays
    // linear instead of quadratic.
    beginResetModel();
    rebuild();
    endResetModel();
}

void ObjectModel::clear() {
    beginResetModel();
    m_bucketMode = false;
    m_buckets.clear();
    m_objects.clear();
    m_rows.clear();
    m_folders.clear();
    endResetModel();
}

void ObjectModel::setPrefix(const QString &prefix) {
    const QString normalised = (prefix == QLatin1String("all")) ? QString() : prefix;
    if (normalised == m_prefix) {
        return;
    }
    beginResetModel();
    m_prefix = normalised;
    rebuild();
    endResetModel();
}

void ObjectModel::setSearchText(const QString &text) {
    if (text == m_search) {
        return;
    }
    beginResetModel();
    m_search = text;
    rebuild();
    endResetModel();
}

void ObjectModel::setFoldersEnabled(bool on) {
    if (on == m_foldersEnabled) {
        return;
    }
    beginResetModel();
    m_foldersEnabled = on;
    rebuild();
    endResetModel();
}

bool ObjectModel::matches(const ObjectInfo &object) const {
    if (!object.key.startsWith(m_prefix)) {
        return false;
    }
    if (m_search.isEmpty()) {
        return true;
    }
    // Case-insensitive substring over the whole key, so typing "log" finds
    // "archive/2024/app.log" without the user navigating there first. This is
    // the same reach the original had, but expressed once rather than as a
    // switch with fallthrough cases.
    return object.key.contains(m_search, Qt::CaseInsensitive);
}

void ObjectModel::rebuildFolders() {
    m_folders.clear();
    if (!m_foldersEnabled || !m_search.isEmpty()) {
        return;
    }

    // Sorted, de-duplicated view of the immediate children of the prefix.
    QMap<QString, bool> seen;
    for (const ObjectInfo &object : m_objects) {
        if (!object.key.startsWith(m_prefix)) {
            continue;
        }
        const QString rest = object.key.mid(m_prefix.size());
        const int slash = rest.indexOf(QLatin1Char('/'));
        if (slash <= 0) {
            continue; // no sub-prefix: this is a file directly under us
        }
        seen.insert(rest.left(slash), true);
    }

    QCollator collator(QLocale::system());
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);

    QStringList names = seen.keys();
    std::sort(names.begin(), names.end(), [&collator](const QString &a, const QString &b) {
        return collator.compare(a, b) < 0;
    });

    for (const QString &name : names) {
        Row row;
        row.isFolder = true;
        row.folderName = name;
        m_folders.append(row);
    }
}

void ObjectModel::rebuild() {
    rebuildFolders();

    m_rows.clear();

    // Bucket mode is a different list, not a different prefix: there is no
    // prefix, no folder grouping and nothing to sort by size or date, so the
    // object path below is skipped entirely rather than run over an empty list.
    if (m_bucketMode) {
        QCollator collator(QLocale::system());
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);

        QList<int> order;
        order.reserve(m_buckets.size());
        for (int i = 0; i < m_buckets.size(); ++i) {
            order.append(i);
        }
        if (m_search.isEmpty()) {
            std::stable_sort(order.begin(), order.end(), [&](int lhs, int rhs) {
                const int cmp = collator.compare(m_buckets.at(lhs).name, m_buckets.at(rhs).name);
                return m_order == Qt::AscendingOrder ? cmp < 0 : cmp > 0;
            });
        }

        m_rows.reserve(order.size());
        for (int index : order) {
            const BucketInfo &bucket = m_buckets.at(index);
            if (!m_search.isEmpty() && !bucket.name.contains(m_search, Qt::CaseInsensitive)) {
                continue;
            }
            Row row;
            row.isBucket = true;
            row.bucketIndex = index;
            m_rows.append(row);
        }
        return;
    }

    m_rows.reserve(m_folders.size() + m_objects.size());

    if (m_foldersEnabled && m_search.isEmpty()) {
        m_rows.append(m_folders);
    }

    QCollator collator(QLocale::system());
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);

    // Filter, keeping only indices, then sort that list. Nothing copies an
    // ObjectInfo, which is what keeps a 50k-object list responsive while the
    // user types.
    QList<int> indices;
    indices.reserve(m_objects.size());
    for (int i = 0; i < m_objects.size(); ++i) {
        if (matches(m_objects.at(i))) {
            indices.append(i);
        }
    }

    const int column = m_sortColumn;
    const bool ascending = m_order == Qt::AscendingOrder;

    std::stable_sort(indices.begin(), indices.end(), [&](int lhs, int rhs) {
        const ObjectInfo &a = m_objects.at(lhs);
        const ObjectInfo &b = m_objects.at(rhs);

        int cmp = 0;
        switch (column) {
        case SizeColumn:
            cmp = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
            break;
        case ModifiedColumn:
            cmp = a.lastModified < b.lastModified ? -1 : (a.lastModified > b.lastModified ? 1 : 0);
            break;
        case NameColumn:
        default: {
            // Compare the visible portion of the key, so sorting matches what
            // the user sees rather than an invisible shared prefix.
            const QString sa = a.key.mid(m_prefix.size());
            const QString sb = b.key.mid(m_prefix.size());
            cmp = collator.compare(sa, sb);
            break;
        }
        }

        if (cmp == 0) {
            // Stable tiebreak so equal sizes keep a predictable order.
            cmp = collator.compare(a.key, b.key);
        }
        return ascending ? cmp < 0 : cmp > 0;
    });

    for (int index : indices) {
        Row row;
        row.isFolder = false;
        row.objectIndex = index;
        m_rows.append(row);
    }

    // Folders always lead, in name order, regardless of the active sort: they
    // are navigation, not content, and Explorer-trained users expect them first.
}

void ObjectModel::sort(int column, Qt::SortOrder order) {
    if (column < 0 || column >= ColumnCount) {
        return;
    }
    beginResetModel();
    m_sortColumn = column;
    m_order = order;
    rebuild();
    endResetModel();

    emit headerDataChanged(Qt::Horizontal, 0, ColumnCount - 1);
}

} // namespace s3desktop
