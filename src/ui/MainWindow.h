#pragma once

#include "core/S3Config.h"
#include "core/S3Types.h"

#include <QMainWindow>
#include <QStringList>

class QAction;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
class QToolBar;

namespace us3 {

class BucketManager;
class ConnectDialog;
class ConnectionStore;
class ObjectBrowser;
class ObjectModel;
class S3Client;
class TransferPanel;
class TransferQueue;

/// The application window: ribbon, browser, transfer dock, status bar.
///
/// Navigation state lives in the browser; network state lives here. Everything
/// that decides *whether* a request is allowed to go out — is there a bucket, is
/// a load already running, did the user change connection — belongs to this
/// class, so there is exactly one place to reason about it.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /// Apply a connection: reconfigure the client, clear the browser, load.
    void connectWith(const S3Config &config);

private slots:
    void onConnectDialog();
    void onLoadRequested(const QString &prefix);
    void onLoadMore();
    void onUpload();
    void onDownload();
    void onDelete();
    void onCopyLink();
    void onCopyKeys();
    void onRefresh();
    void onSelectionChanged();
    void onSearchChanged(const QString &text);
    void onManageBuckets();
    void onAbout();
    void onCheckVersion();
    void onToggleTransfers();
    void onToggleDetails();
    void onContextMenu(const QPoint &pos);
    void onTransferFinished(int itemId, bool ok);

private:
    void buildActions();
    void buildMenus();
    void buildRibbon();
    void buildStatusBar();
    void applyConfigToUi();

    /// Start a fresh listing at the current prefix.
    void reload();

    /// Ask for the next page using the last key seen.
    void loadPage(const QString &startAfter);

    void setBusy(bool busy);
    void setStatusMessage(const QString &text, bool isError = false);
    void updateWindowTitle();
    void updateCommandStates();
    void refreshStatusCounts();

    /// The key prefix an upload should be placed under: the current folder.
    QString uploadTargetKey(const QString &fileName) const;

    /// Ask for a destination directory once for a batch of downloads.
    bool chooseDownloadDir(QString *dir);

    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

    ConnectionStore *m_store = nullptr;
    S3Client *m_client = nullptr;
    TransferQueue *m_queue = nullptr;

    ObjectBrowser *m_browser = nullptr;
    ObjectModel *m_model = nullptr;
    TransferPanel *m_transfers = nullptr;

    S3Config m_config;

    // --- Actions ---
    QAction *m_actConnect = nullptr;
    QAction *m_actUpload = nullptr;
    QAction *m_actDownload = nullptr;
    QAction *m_actDelete = nullptr;
    QAction *m_actLink = nullptr;
    QAction *m_actCopyKeys = nullptr;
    QAction *m_actRefresh = nullptr;
    QAction *m_actLoadMore = nullptr;
    QAction *m_actStop = nullptr;
    QAction *m_actSelectAll = nullptr;
    QAction *m_actSearch = nullptr;
    QAction *m_actExit = nullptr;
    QAction *m_actBuckets = nullptr;
    QAction *m_actDetails = nullptr;
    QAction *m_actTransfers = nullptr;
    QAction *m_actColumns = nullptr;
    QAction *m_actSortName = nullptr;
    QAction *m_actSortSize = nullptr;
    QAction *m_actSortDate = nullptr;
    QAction *m_actSortAsc = nullptr;
    QAction *m_actSortDesc = nullptr;
    QAction *m_actVersion = nullptr;
    QAction *m_actAbout = nullptr;

    // --- Status bar ---
    QLabel *m_statusCounts = nullptr;
    QLabel *m_statusConnection = nullptr;
    QLabel *m_statusMessage = nullptr;
    QProgressBar *m_statusProgress = nullptr;

    // --- Listing state ---
    bool m_loading = false;
    bool m_truncated = false;
    QString m_lastKey;
    int m_requestId = 0;
    int m_objectCap = 50000;
    QTimer *m_searchDebounce = nullptr;

    /// Files dropped while no connection was open, held so the drop is not lost.
    QStringList m_pendingDrop;
};

} // namespace us3
