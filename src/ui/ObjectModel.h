#pragma once

#include "core/S3Types.h"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QList>
#include <QString>

namespace us3 {

/// The object table's data model.
///
/// Three things here are deliberate departures from the original, which rebuilt
/// its entire filter result and then refreshed the whole widget on every
/// keystroke and every selection change:
///
///  1. The raw object list is kept whole and a separate index list holds what is
///     currently visible. Filtering and sorting reorder the index list, which is
///     cheap, instead of copying ObjectInfo values.
///  2. Sorting is real. The original showed a sort arrow but re-sorted nothing.
///  3. Folders are synthesised from the key prefixes under the current prefix, so
///     a namespace with a `/` layout is navigable by clicking rather than by
///     typing a prefix into a box.
class ObjectModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { NameColumn, SizeColumn, ModifiedColumn, ColumnCount };
    enum Roles {
        KeyRole = Qt::UserRole + 1,  ///< full object key, empty for a folder row
        SizeRole,                    ///< qint64
        DateRole,                    ///< QDateTime
        IsFolderRole,                ///< bool
        FolderNameRole,              ///< QString
        EtagRole,
        StorageClassRole,
        RawIndexRole,                ///< index into the underlying object list
    };

    explicit ObjectModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    /// Replace the loaded objects. Clears the folder rows and re-filters.
    void setObjects(const QList<ObjectInfo> &objects);

    /// Append one page. Used by "load more" so the table grows without flicker.
    void appendObjects(const QList<ObjectInfo> &objects);

    /// Drop everything. Also resets the truncation flag.
    void clear();

    int objectCount() const { return m_objects.size(); }
    int visibleCount() const { return m_rows.size(); }
    int folderCount() const;

    /// The key for a row, or the folder pseudo-key for a folder row
    /// (prefix + name + "/"), which is what navigation needs.
    QString keyAt(const QModelIndex &index) const;

    /// Every visible object, in view order. Used by "select all" and by bulk
    /// delete, which must operate on what the user can see.
    QList<ObjectInfo> visibleObjects() const;

    /// The object behind a row. Returns false for a folder row.
    bool objectAt(const QModelIndex &index, ObjectInfo *out) const;

    // --- Filtering ---------------------------------------------------------

    /// The prefix whose contents are on screen. Empty or "all" means the root.
    void setPrefix(const QString &prefix);
    QString prefix() const { return m_prefix; }

    /// Free text. Matched case-insensitively against the whole key, which is
    /// what lets a search reach into sub-prefixes the user has not navigated to.
    void setSearchText(const QString &text);
    QString searchText() const { return m_search; }

    /// Whether synthetic folder rows appear. Off when searching, because a
    /// search result set is a flat list of matches.
    void setFoldersEnabled(bool on);

    Qt::SortOrder sortOrder() const { return m_order; }
    int sortColumn() const { return m_sortColumn; }

private:
    void rebuild();
    void rebuildFolders();
    bool matches(const ObjectInfo &object) const;

public:
    /// One visible line: either a synthesised folder or an object.
    struct Row {
        bool isFolder = false;
        QString folderName;   ///< last path segment, for folders
        int objectIndex = -1; ///< index into m_objects, for objects
    };

private:
    QList<ObjectInfo> m_objects;
    QList<Row> m_rows;
    QList<Row> m_folders; ///< recomputed only when the prefix or objects change

    QString m_prefix;
    QString m_search;
    bool m_foldersEnabled = true;

    int m_sortColumn = NameColumn;
    Qt::SortOrder m_order = Qt::AscendingOrder;
};

} // namespace us3
