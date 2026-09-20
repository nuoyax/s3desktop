#pragma once

#include "core/S3Types.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QTableView;
class QToolButton;

namespace us3 {

class ObjectModel;
class S3Client;

/// The main content area: navigation, the object table, and the details pane.
///
/// This widget owns no network state of its own beyond a pointer to the client;
/// MainWindow decides *when* to load, and this widget decides *what the user is
/// looking at*. Splitting it that way keeps the "load more" and breadcrumb
/// history logic in one place rather than spread across the window.
class ObjectBrowser : public QWidget {
    Q_OBJECT

public:
    explicit ObjectBrowser(S3Client *client, QWidget *parent = nullptr);

    ObjectModel *model() const { return m_model; }
    QTableView *view() const { return m_table; }

    /// The prefix currently displayed, as a key prefix ("" for the root).
    QString prefix() const { return m_prefix; }

    /// Show `prefix` and emit loadRequested. Pushes onto the back/forward
    /// history so the nav buttons work like a browser's.
    void navigateTo(const QString &prefix);

    /// Move one level up. No-op at the root.
    void navigateUp();

    /// Return to the root prefix, clearing the back/forward history. Used when a
    /// new connection is applied, so history from the previous bucket cannot be
    /// walked back into.
    void resetNavigation();

    /// The bucket whose objects are on screen, empty while the bucket list is
    /// what is on screen. Drives the breadcrumb: a bucket is a level above every
    /// key prefix, so it has to appear in the path.
    void setBucketName(const QString &bucket);

    /// Whether this connection can list buckets, and so whether the root crumb
    /// leads to the bucket list. False for a connection pinned to one bucket,
    /// where the root is that bucket.
    void setBucketListAvailable(bool on);

    /// Note that the table is showing the account's buckets rather than objects,
    /// which changes the column headings and the search hint.
    void setShowingBuckets(bool on);

    bool canGoBack() const { return m_historyIndex > 0; }
    bool canGoForward() const { return m_historyIndex + 1 < m_history.size(); }

    /// Whether there is a level above the current one — which at a bucket's root
    /// is the bucket list, not a shorter prefix.
    bool canGoUp() const;

    void goBack();
    void goForward();

    /// Keys selected in the table, folders excluded.
    QStringList selectedKeys() const;


    /// Objects selected in the table.
    QList<ObjectInfo> selectedObjects() const;

    /// Select every visible row.
    void selectAll();

    /// Show `object` in the details pane; an invalid index clears it.
    void showDetailsFor(const QModelIndex &index);

    /// The details pane visibility, toggled from the ribbon.
    void setDetailsVisible(bool on);
    bool detailsVisible() const;

    /// Reflect an active search in the box without emitting a new search
    /// (used when the window restores state).
    void setSearchText(const QString &text);

signals:
    /// The user asked for the contents of `prefix`.
    void loadRequested(const QString &prefix);

    /// The user typed in the search box. Debouncing is the window's job, since
    /// it also has to decide whether a re-list is needed.
    void searchChanged(const QString &text);

    /// A folder row was double-clicked.
    void folderActivated(const QString &prefix);

    /// An object row was double-clicked.
    void objectActivated(const QString &key);

    /// A bucket row was double-clicked: the user wants to browse into it.
    void bucketActivated(const QString &bucket);

    /// The user navigated up out of a bucket, or clicked the "Buckets" crumb:
    /// the window should show the bucket list again.
    void bucketsRequested();

    /// The selection changed; the window uses this to enable/disable commands.
    void selectionChanged();

private slots:
    void onTableActivated(const QModelIndex &index);

private:
    void buildUi();
    QWidget *buildBreadcrumb();
    QWidget *buildDetailsPane();
    void rebuildBreadcrumb();
    void pushHistory(const QString &prefix);

    /// The body of navigateTo(). `push` is false when the prefix came from the
    /// history rather than from the user choosing it again.
    void performNavigation(const QString &prefix, bool push);

    S3Client *m_client = nullptr;
    ObjectModel *m_model = nullptr;

    QToolButton *m_back = nullptr;
    QToolButton *m_forward = nullptr;
    QToolButton *m_up = nullptr;
    QWidget *m_crumbHost = nullptr;
    QLineEdit *m_search = nullptr;

    QTableView *m_table = nullptr;

    QWidget *m_details = nullptr;
    QLabel *m_detailName = nullptr;
    QLabel *m_detailSize = nullptr;
    QLabel *m_detailModified = nullptr;
    QLabel *m_detailEtag = nullptr;
    QLabel *m_detailClass = nullptr;
    QLabel *m_detailKey = nullptr;

    QString m_prefix;
    QString m_bucket;
    bool m_bucketListAvailable = false;
    bool m_showingBuckets = false;
    QStringList m_history;
    int m_historyIndex = -1;
};

} // namespace us3
