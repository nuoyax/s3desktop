#include "ui/MainWindow.h"

#include "compat/TargetProfile.h"
#include "core/ConnectionStore.h"
#include "core/S3Client.h"
#include "core/TransferQueue.h"
#include "ui/BucketManager.h"
#include "ui/ConnectDialog.h"
#include "ui/ObjectBrowser.h"
#include "ui/ObjectModel.h"
#include "ui/Theme.h"
#include "ui/TransferPanel.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressBar>
#include <QStyle>
#include <QSettings>
#include <QItemSelectionModel>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace us3 {

namespace {

/// Bumped by hand; the version check below compares against the upstream
/// repository's FyneApp.toml, which is the only release marker the original
/// project publishes.
const char *kVersion = "0.1.0";

const char *kProjectUrl = "https://github.com/pteich/us3ui";
const char *kVersionUrl = "https://raw.githubusercontent.com/pteich/us3ui/refs/heads/main/FyneApp.toml";

/// Page size for a listing. The original used 500 and updated the UI every
/// 500ms; here the page size is the same because it balances request count
/// against time-to-first-row, but repaints are driven by the model instead of a
/// timer.
constexpr int kPageSize = 500;

QString elide(const QString &text, int limit = 60) {
    return text.size() <= limit ? text : text.left(limit - 1) + QChar(0x2026);
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("US3"));
    setAcceptDrops(true);
    resize(1180, 720);

    m_store = new ConnectionStore;
    m_client = new S3Client(this);
    m_queue = new TransferQueue(this);

    buildActions();
    buildMenus();
    buildRibbon();
    buildStatusBar();

    // The browser occupies the centre; the transfer dock is a child widget
    // rather than a QDockWidget so it can share the Fluent styling without
    // fighting QDockWidget's own title bar.
    auto *central = new QWidget;
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    m_browser = new ObjectBrowser(m_client, central);
    m_model = m_browser->model();
    centralLayout->addWidget(m_browser, 1);

    m_transfers = new TransferPanel(m_queue, central);
    m_transfers->setVisible(false);
    centralLayout->addWidget(m_transfers);

    setCentralWidget(central);

    connect(m_browser, &ObjectBrowser::loadRequested, this, &MainWindow::onLoadRequested);
    connect(m_browser, &ObjectBrowser::searchChanged, this, &MainWindow::onSearchChanged);
    connect(m_browser, &ObjectBrowser::selectionChanged, this, &MainWindow::onSelectionChanged);
    // Double-clicking an object starts a download, which is what the original
    // did on Enter and what most file managers do on a double-click.
    connect(m_browser, &ObjectBrowser::objectActivated, this, &MainWindow::onDownload);
    connect(m_browser->view(), &QTableView::customContextMenuRequested, this,
            &MainWindow::onContextMenu);
    connect(m_browser->view()->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &) {
                m_browser->showDetailsFor(current);
            });

    connect(m_transfers, &TransferPanel::closeRequested, this, [this]() {
        m_actTransfers->setChecked(false);
        m_transfers->setVisible(false);
    });

    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(220);
    connect(m_searchDebounce, &QTimer::timeout, this, [this]() {
        m_model->setSearchText(m_browser->view()->property("pendingSearch").toString());
        updateCommandStates();
    });

    m_queue->setClient(m_client);
    connect(m_queue, &TransferQueue::finished, this, &MainWindow::onTransferFinished);
    connect(m_queue, &TransferQueue::changed, this, [this]() {
        if (m_transfers->isVisible()) {
            return; // the panel refreshes itself
        }
        // With the dock hidden the status bar carries the only progress there
        // is, so it has to stay live.
        int active = 0;
        qint64 done = 0;
        qint64 total = 0;
        for (const TransferItem &item : m_queue->items()) {
            if (item.state == TransferItem::State::Queued ||
                item.state == TransferItem::State::Running) {
                ++active;
                done += item.bytesDone;
                total += item.bytesTotal;
            }
        }
        m_statusProgress->setVisible(active > 0);
        if (active > 0) {
            m_statusProgress->setRange(0, total > 0 ? 1000 : 0);
            m_statusProgress->setValue(total > 0 ? int(done * 1000 / total) : 0);
            setStatusMessage(QStringLiteral("%1 transfer(s) in progress").arg(active));
        }
    });

    QString warning;
    if (!m_store->load(&warning)) {
        QMessageBox::warning(this, QStringLiteral("Configuration"),
                             QStringLiteral("Settings could not be read.\n\n%1").arg(warning));
    } else if (!warning.isEmpty()) {
        setStatusMessage(warning, true);
    }

    // A connection described entirely by the environment skips the dialog, which
    // is what makes the tool usable from a script or a shortcut.
    QStringList transientWarnings;
    const S3Config transient = ConnectionStore::fromCommandLine(&transientWarnings);
    for (const QString &note : transientWarnings) {
        setStatusMessage(note, true);
    }
    if (!transient.endpoint.isEmpty() && !transient.accessKey.isEmpty()) {
        connectWith(transient);
    }

    applyConfigToUi();
    updateCommandStates();
}

MainWindow::~MainWindow() { delete m_store; }

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void MainWindow::buildActions() {
    auto make = [](const QString &text, Theme::Glyph glyph, const QKeySequence &shortcut) {
        auto *action = new QAction(text, nullptr);
        action->setIcon(Theme::icon(glyph));
        if (!shortcut.isEmpty()) {
            action->setShortcut(shortcut);
        }
        return action;
    };

    m_actConnect = make(QStringLiteral("&Connect…"), Theme::Glyph::Plug, QKeySequence(QStringLiteral("Ctrl+K")));
    m_actUpload = make(QStringLiteral("&Upload…"), Theme::Glyph::Upload, QKeySequence(QStringLiteral("Ctrl+U")));
    m_actDownload = make(QStringLiteral("&Download…"), Theme::Glyph::Download, QKeySequence(QStringLiteral("Ctrl+D")));
    m_actDelete = make(QStringLiteral("De&lete"), Theme::Glyph::Delete, QKeySequence(QKeySequence::Delete));
    m_actLink = make(QStringLiteral("Copy &link"), Theme::Glyph::Link, QKeySequence(QStringLiteral("Ctrl+L")));
    m_actCopyKeys = make(QStringLiteral("Copy &keys"), Theme::Glyph::Copy, QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    m_actRefresh = make(QStringLiteral("&Refresh"), Theme::Glyph::Refresh, QKeySequence(QKeySequence::Refresh));
    m_actLoadMore = make(QStringLiteral("Load &more"), Theme::Glyph::Add, QKeySequence(QStringLiteral("Ctrl+M")));
    m_actStop = make(QStringLiteral("&Stop loading"), Theme::Glyph::Stop, QKeySequence(QStringLiteral("Esc")));
    m_actSelectAll = make(QStringLiteral("Select &all"), Theme::Glyph::Check, QKeySequence(QKeySequence::SelectAll));
    m_actSearch = make(QStringLiteral("&Find…"), Theme::Glyph::Search, QKeySequence(QKeySequence::Find));
    m_actExit = new QAction(QStringLiteral("E&xit"), this);
    m_actExit->setShortcut(QKeySequence::Quit);

    m_actBuckets = make(QStringLiteral("&Buckets…"), Theme::Glyph::Bucket, QKeySequence(QStringLiteral("Ctrl+B")));
    m_actDetails = make(QStringLiteral("&Details pane"), Theme::Glyph::Columns, QKeySequence(QStringLiteral("Ctrl+I")));
    m_actDetails->setCheckable(true);
    m_actDetails->setChecked(true);
    m_actTransfers = make(QStringLiteral("&Transfers"), Theme::Glyph::Upload, QKeySequence(QStringLiteral("Ctrl+J")));
    m_actTransfers->setCheckable(true);
    m_actColumns = make(QStringLiteral("C&olumns…"), Theme::Glyph::Columns, QKeySequence());

    auto *sortGroup = new QActionGroup(this);
    m_actSortName = make(QStringLiteral("by &name"), Theme::Glyph::SortAsc, QKeySequence());
    m_actSortSize = make(QStringLiteral("by &size"), Theme::Glyph::Size, QKeySequence());
    m_actSortDate = make(QStringLiteral("by &date"), Theme::Glyph::Clock, QKeySequence());
    for (QAction *action : {m_actSortName, m_actSortSize, m_actSortDate}) {
        action->setCheckable(true);
        sortGroup->addAction(action);
    }
    m_actSortName->setChecked(true);

    auto *orderGroup = new QActionGroup(this);
    m_actSortAsc = make(QStringLiteral("&Ascending"), Theme::Glyph::SortAsc, QKeySequence());
    m_actSortDesc = make(QStringLiteral("&Descending"), Theme::Glyph::SortDesc, QKeySequence());
    for (QAction *action : {m_actSortAsc, m_actSortDesc}) {
        action->setCheckable(true);
        orderGroup->addAction(action);
    }
    m_actSortAsc->setChecked(true);

    m_actVersion = new QAction(QStringLiteral("Check for &updates…"), this);
    m_actAbout = new QAction(QStringLiteral("&About US3"), this);

    connect(m_actConnect, &QAction::triggered, this, &MainWindow::onConnectDialog);
    connect(m_actUpload, &QAction::triggered, this, &MainWindow::onUpload);
    connect(m_actDownload, &QAction::triggered, this, &MainWindow::onDownload);
    connect(m_actDelete, &QAction::triggered, this, &MainWindow::onDelete);
    connect(m_actLink, &QAction::triggered, this, &MainWindow::onCopyLink);
    connect(m_actCopyKeys, &QAction::triggered, this, &MainWindow::onCopyKeys);
    connect(m_actRefresh, &QAction::triggered, this, &MainWindow::onRefresh);
    connect(m_actLoadMore, &QAction::triggered, this, &MainWindow::onLoadMore);
    connect(m_actStop, &QAction::triggered, this, [this]() {
        if (!m_loading) {
            return;
        }
        m_client->cancel(m_requestId);
    });
    connect(m_actSelectAll, &QAction::triggered, this, [this]() { m_browser->selectAll(); });
    connect(m_actSearch, &QAction::triggered, this, [this]() {
        m_browser->findChild<QLineEdit *>(QStringLiteral("search"))->setFocus();
    });
    connect(m_actExit, &QAction::triggered, this, &QWidget::close);
    connect(m_actBuckets, &QAction::triggered, this, &MainWindow::onManageBuckets);
    connect(m_actColumns, &QAction::triggered, this, [this]() {
        QMessageBox::information(
            this, QStringLiteral("Columns"),
            QStringLiteral("The object table shows Name, Size and Modified.\n\n"
                           "Drag a column divider to resize it, or click a header to sort by "
                           "that column."));
    });
    connect(m_actVersion, &QAction::triggered, this, &MainWindow::onCheckVersion);
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::onAbout);

    connect(m_actDetails, &QAction::toggled, this, &MainWindow::onToggleDetails);
    connect(m_actTransfers, &QAction::toggled, this, &MainWindow::onToggleTransfers);

    auto applySort = [this]() {
        int column = ObjectModel::NameColumn;
        if (m_actSortSize->isChecked()) {
            column = ObjectModel::SizeColumn;
        } else if (m_actSortDate->isChecked()) {
            column = ObjectModel::ModifiedColumn;
        }
        const Qt::SortOrder order =
            m_actSortDesc->isChecked() ? Qt::DescendingOrder : Qt::AscendingOrder;
        m_browser->view()->sortByColumn(column, order);
    };
    connect(m_actSortName, &QAction::triggered, this, applySort);
    connect(m_actSortSize, &QAction::triggered, this, applySort);
    connect(m_actSortDate, &QAction::triggered, this, applySort);
    connect(m_actSortAsc, &QAction::triggered, this, applySort);
    connect(m_actSortDesc, &QAction::triggered, this, applySort);
}

void MainWindow::buildMenus() {
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    file->addAction(m_actConnect);
    file->addSeparator();
    file->addAction(m_actUpload);
    file->addAction(m_actDownload);
    file->addAction(m_actDelete);
    file->addSeparator();
    file->addAction(m_actLink);
    file->addAction(m_actCopyKeys);
    file->addSeparator();
    file->addAction(m_actExit);

    QMenu *edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    edit->addAction(m_actSelectAll);
    edit->addAction(m_actSearch);
    edit->addSeparator();
    edit->addAction(m_actCopyKeys);

    QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
    view->addAction(m_actRefresh);
    view->addAction(m_actLoadMore);
    view->addAction(m_actStop);
    view->addSeparator();
    view->addAction(m_actDetails);
    view->addAction(m_actTransfers);
    view->addAction(m_actColumns);
    view->addSeparator();
    view->addAction(m_actSortName);
    view->addAction(m_actSortSize);
    view->addAction(m_actSortDate);
    view->addSeparator();
    view->addAction(m_actSortAsc);
    view->addAction(m_actSortDesc);

    QMenu *bucket = menuBar()->addMenu(QStringLiteral("&Bucket"));
    bucket->addAction(m_actBuckets);
    bucket->addAction(m_actRefresh);

    QMenu *tools = menuBar()->addMenu(QStringLiteral("&Tools"));
    tools->addAction(m_actConnect);

    QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(m_actVersion);
    help->addAction(m_actAbout);
}

void MainWindow::buildRibbon() {
    auto *bar = new QWidget;
    bar->setObjectName(QStringLiteral("ribbon"));
    bar->setFixedHeight(Theme::ribbonHeight);

    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(6, 4, 8, 4);
    layout->setSpacing(0);

    // A ribbon group: a caption under a row of icon buttons, separated by a
    // hairline. This is the Explorer "command bar" rather than the Office
    // ribbon — same affordance, far less vertical space.
    auto addGroup = [&](const QString &caption, const QList<QAction *> &actions) {
        auto *group = new QWidget;
        auto *groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(4, 0, 4, 0);
        groupLayout->setSpacing(0);

        auto *row = new QHBoxLayout;
        row->setSpacing(2);
        for (QAction *action : actions) {
            auto *button = new QToolButton;
            button->setObjectName(QStringLiteral("ribbonButton"));
            button->setDefaultAction(action);
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setIconSize(QSize(20, 20));
            row->addWidget(button);
        }
        groupLayout->addLayout(row);

        auto *label = new QLabel(caption);
        label->setObjectName(QStringLiteral("hint"));
        label->setAlignment(Qt::AlignCenter);
        groupLayout->addWidget(label);

        layout->addWidget(group);

        auto *separator = new QFrame;
        separator->setObjectName(QStringLiteral("ribbonGroup"));
        separator->setFrameShape(QFrame::VLine);
        separator->setFixedWidth(1);
        layout->addWidget(separator);
    };

    addGroup(QStringLiteral("Object"),
             {m_actUpload, m_actDownload, m_actDelete, m_actLink});
    addGroup(QStringLiteral("View"),
             {m_actRefresh, m_actLoadMore, m_actStop});
    addGroup(QStringLiteral("Navigate"),
             {m_actConnect, m_actBuckets});
    layout->addStretch(1);

    auto *toolbar = new QToolBar;
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->addWidget(bar);
    addToolBar(Qt::TopToolBarArea, toolbar);
}

void MainWindow::buildStatusBar() {
    m_statusMessage = new QLabel;
    m_statusCounts = new QLabel;
    m_statusConnection = new QLabel;

    m_statusProgress = new QProgressBar;
    m_statusProgress->setFixedWidth(160);
    m_statusProgress->setTextVisible(false);
    m_statusProgress->setVisible(false);

    statusBar()->addWidget(m_statusMessage, 1);
    statusBar()->addPermanentWidget(m_statusProgress);
    statusBar()->addPermanentWidget(m_statusCounts);
    statusBar()->addPermanentWidget(m_statusConnection);
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

void MainWindow::onConnectDialog() {
    auto *dialog = new ConnectDialog(m_store, m_client, this);

    connect(dialog, &ConnectDialog::manageBucketsRequested, this,
            [this, dialog](const S3Config &config) {
                auto *manager = new BucketManager(this);
                manager->setConfig(config);
                manager->setAttribute(Qt::WA_DeleteOnClose);
                if (manager->exec() == QDialog::Accepted) {
                    dialog->preselect(dialog->selectedConfig().name);
                }
            });

    if (dialog->exec() == QDialog::Accepted) {
        connectWith(dialog->selectedConfig());
    }
    dialog->deleteLater();
}

void MainWindow::connectWith(const S3Config &config) {
    // Everything in flight belongs to the old connection; leaving it running
    // would deliver results into a browser that now shows a different bucket.
    m_client->cancelAll();
    m_queue->cancelAll();

    m_config = config;
    m_client->configure(config);

    // Re-enable commands that a previous failure may have disabled.
    m_objectCap = 50000;
    m_truncated = false;
    m_lastKey.clear();
    m_loading = false;

    m_browser->resetNavigation();
    m_model->clear();

    applyConfigToUi();
    updateCommandStates();

    const QString problem = config.validate();
    if (!problem.isEmpty()) {
        setStatusMessage(problem, true);
        return;
    }

    // With no bucket chosen, show what the account can see by listing buckets
    // and letting the user pick — the same entry point the original offered,
    // but inline rather than in a separate window.
    if (config.bucket.isEmpty()) {
        setStatusMessage(QStringLiteral("No bucket selected — listing buckets…"));
        m_client->listBuckets([this](const Result<QList<BucketInfo>> &result) {
            if (!result.ok()) {
                setStatusMessage(result.error.message(), true);
                return;
            }
            if (result.value.isEmpty()) {
                setStatusMessage(QStringLiteral("This account has no buckets."), true);
                return;
            }

            QStringList names;
            for (const BucketInfo &bucket : result.value) {
                names.append(bucket.name);
            }

            bool accepted = false;
            const QString choice = QInputDialog::getItem(
                this, QStringLiteral("Choose a bucket"),
                QStringLiteral("This connection does not name a bucket. Pick one:"), names, 0,
                false, &accepted);

            if (accepted && !choice.isEmpty()) {
                m_config.bucket = choice;
                m_client->configure(m_config);
                applyConfigToUi();
                reload();
            } else {
                setStatusMessage(QStringLiteral("No bucket selected."));
            }
        });
        return;
    }

    reload();
}

void MainWindow::applyConfigToUi() {
    updateWindowTitle();

    const QString problem = m_config.validate();
    if (m_config.name.isEmpty() && m_config.endpoint.isEmpty()) {
        m_statusConnection->setText(QStringLiteral("not connected"));
        m_statusConnection->setToolTip(QString());
        return;
    }

    const TargetProfile profile = TargetProfile::byId(m_config.targetId);
    m_statusConnection->setText(QStringLiteral("%1 · %2 · %3")
                                    .arg(m_config.name.isEmpty() ? QStringLiteral("(unnamed)")
                                                                 : m_config.name)
                                    .arg(m_config.bucket.isEmpty() ? QStringLiteral("no bucket")
                                                                   : m_config.bucket)
                                    .arg(m_config.effectiveUseSsl() ? QStringLiteral("https")
                                                                    : QStringLiteral("http")));
    m_statusConnection->setToolTip(
        QStringLiteral("Endpoint: %1\nRegion: %2\nProfile: %3\nAddressing: %4\n\n%5")
            .arg(m_config.endpoint)
            .arg(m_client->effectiveRegion())
            .arg(profile.displayName)
            .arg(m_config.addressingStyle == int(AddressingStyle::VirtualHost)
                     ? QStringLiteral("virtual host")
                     : QStringLiteral("path style"))
            .arg(problem.isEmpty() ? QStringLiteral("Ready.") : problem));
}

void MainWindow::updateWindowTitle() {
    QString title = QStringLiteral("US3");
    if (!m_config.name.isEmpty()) {
        title += QStringLiteral(" — ") + m_config.name;
    }
    if (!m_config.bucket.isEmpty()) {
        title += QStringLiteral(" / ") + m_config.bucket;
        if (!m_browser->prefix().isEmpty()) {
            title += QStringLiteral("/") + m_browser->prefix();
        }
    }
    setWindowTitle(title);
}

// ---------------------------------------------------------------------------
// Listing
// ---------------------------------------------------------------------------

void MainWindow::onLoadRequested(const QString &prefix) {
    m_config.prefix = prefix;
    updateWindowTitle();
    reload();
}

void MainWindow::reload() {
    if (m_loading) {
        m_client->cancel(m_requestId);
    }

    m_model->clear();
    m_truncated = false;
    m_lastKey.clear();
    loadPage(QString());
}

void MainWindow::onRefresh() {
    if (m_config.bucket.isEmpty()) {
        setStatusMessage(QStringLiteral("Choose a connection and bucket first."), true);
        return;
    }
    reload();
}

void MainWindow::loadPage(const QString &startAfter) {
    if (m_config.bucket.isEmpty()) {
        setStatusMessage(QStringLiteral("No bucket selected."), true);
        return;
    }

    const QString problem = m_config.validate();
    if (!problem.isEmpty()) {
        setStatusMessage(problem, true);
        return;
    }

    setBusy(true);
    if (startAfter.isEmpty()) {
        setStatusMessage(QStringLiteral("Loading…"));
    } else {
        setStatusMessage(QStringLiteral("Loading more…"));
    }

    const QString prefix = m_browser->prefix();
    m_requestId = m_client->listObjects(
        startAfter, prefix, kPageSize,
        [this, startAfter](const FlatListResult &result) {
            setBusy(false);

            if (!result.ok()) {
                if (result.error.kind() == ErrorKind::Cancelled) {
                    setStatusMessage(QStringLiteral("Loading stopped."));
                    return;
                }
                setStatusMessage(result.error.message(), true);
                // A configuration problem is usually an endpoint or region
                // mismatch; pointing at the provider profile is more useful
                // than repeating the server's signature error.
                if (result.error.isConfigurationProblem()) {
                    setStatusMessage(
                        QStringLiteral("%1 — check the provider profile and region in the "
                                       "connection dialog.")
                            .arg(result.error.message()),
                        true);
                }
                return;
            }

            if (startAfter.isEmpty()) {
                m_model->setObjects(result.objects);
            } else {
                m_model->appendObjects(result.objects);
            }

            m_truncated = result.isTruncated;
            if (!result.objects.isEmpty()) {
                m_lastKey = result.objects.last().key;
            }

            if (m_model->objectCount() >= m_objectCap) {
                m_truncated = false;
                setStatusMessage(
                    QStringLiteral("Showing the first %1 objects. Use search or a prefix to "
                                   "narrow the list.")
                        .arg(m_objectCap),
                    true);
            }

            updateCommandStates();
            updateWindowTitle();
            refreshStatusCounts();
        });
}

void MainWindow::onLoadMore() {
    if (m_loading || !m_truncated || m_lastKey.isEmpty()) {
        return;
    }
    loadPage(m_lastKey);
}

void MainWindow::refreshStatusCounts() {
    const int visible = m_model->visibleCount();
    const int total = m_model->objectCount();
    const int folders = m_model->folderCount();
    const int selected = m_browser->selectedKeys().size();

    QString counts = QStringLiteral("%1 item%2").arg(visible).arg(visible == 1 ? "" : "s");
    if (folders > 0) {
        counts += QStringLiteral(" (%1 folder%2)").arg(folders).arg(folders == 1 ? "" : "s");
    }
    if (total != visible) {
        counts += QStringLiteral(" of %1").arg(total);
    }
    if (!m_truncated && total > 0) {
        counts += QStringLiteral(" · complete");
    }
    if (selected > 0) {
        counts += QStringLiteral(" · %1 selected").arg(selected);
    }

    m_statusCounts->setText(counts);
}

void MainWindow::setBusy(bool busy) {
    m_loading = busy;
    updateCommandStates();

    // The status bar keeps a spinner-free indication: a busy label reads better
    // than an indeterminate bar for a page that usually returns in well under a
    // second.
    m_statusProgress->setVisible(busy && !m_transfers->isVisible());
    if (busy) {
        m_statusProgress->setRange(0, 0);
    }
}

void MainWindow::setStatusMessage(const QString &text, bool isError) {
    m_statusMessage->setText(elide(text, 140));
    m_statusMessage->setToolTip(text);
    m_statusMessage->setObjectName(isError ? QStringLiteral("errLabel")
                                           : QStringLiteral("hint"));
    m_statusMessage->style()->unpolish(m_statusMessage);
    m_statusMessage->style()->polish(m_statusMessage);
}

void MainWindow::updateCommandStates() {
    const bool connected = !m_config.bucket.isEmpty() && m_config.validate().isEmpty();
    const int selected = m_browser->selectedKeys().size();

    m_actUpload->setEnabled(connected);
    m_actDownload->setEnabled(connected && selected > 0);
    m_actDelete->setEnabled(connected && selected > 0);
    m_actLink->setEnabled(connected && selected > 0);
    m_actCopyKeys->setEnabled(selected > 0);
    m_actRefresh->setEnabled(connected);
    m_actLoadMore->setEnabled(connected && m_truncated && !m_loading);
    m_actStop->setEnabled(m_loading);
    m_actSelectAll->setEnabled(connected && m_model->visibleCount() > 0);
    m_actBuckets->setEnabled(!m_config.endpoint.isEmpty());
}

void MainWindow::onSelectionChanged() {
    updateCommandStates();
    refreshStatusCounts();
}

void MainWindow::onSearchChanged(const QString &text) {
    m_searchDebounce->stop();

    // The debounce target is stashed on the view; the lambda in the constructor
    // reads it back. Doing it this way keeps the timer's single owner obvious.
    m_browser->view()->setProperty("pendingSearch", text);
    m_searchDebounce->start();

    // Searching only covers objects already in memory, since a client-side
    // filter cannot reach keys that were never listed. Saying so is better than
    // letting a search silently miss the tail of a large bucket.
    if (text.isEmpty()) {
        setStatusMessage(QString());
    } else if (m_truncated) {
        setStatusMessage(QStringLiteral("Searching the %1 objects loaded so far. Choose "
                                        "\"Load more\" to include the rest.")
                             .arg(m_model->objectCount()));
    }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

QString MainWindow::uploadTargetKey(const QString &fileName) const {
    return m_browser->prefix() + fileName;
}

void MainWindow::onUpload() {
    if (m_config.bucket.isEmpty()) {
        setStatusMessage(QStringLiteral("Choose a connection and bucket first."), true);
        return;
    }

    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Upload files"), QString(), QStringLiteral("All files (*)"));
    if (files.isEmpty()) {
        return;
    }

    for (const QString &path : files) {
        const QString key = uploadTargetKey(QFileInfo(path).fileName());
        m_queue->enqueueUpload(path, key);
    }

    m_actTransfers->setChecked(true);
    setStatusMessage(QStringLiteral("Queued %1 upload(s).").arg(files.size()));
}

void MainWindow::onDownload() {
    const QStringList keys = m_browser->selectedKeys();
    if (keys.isEmpty()) {
        return;
    }

    QString dir;
    if (!chooseDownloadDir(&dir)) {
        return;
    }

    for (const QString &key : keys) {
        m_queue->enqueueDownload(key, dir);
    }

    m_actTransfers->setChecked(true);
    setStatusMessage(QStringLiteral("Queued %1 download(s) into %2.").arg(keys.size()).arg(dir));
}

bool MainWindow::chooseDownloadDir(QString *dir) {
    const QString last = QSettings().value(QStringLiteral("lastDownloadDir")).toString();
    const QString chosen = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Download to…"),
        last.isEmpty() ? QDir::homePath() : last);
    if (chosen.isEmpty()) {
        return false;
    }
    QSettings().setValue(QStringLiteral("lastDownloadDir"), chosen);
    *dir = chosen;
    return true;
}

void MainWindow::onDelete() {
    const QStringList keys = m_browser->selectedKeys();
    if (keys.isEmpty()) {
        return;
    }

    const auto answer = QMessageBox::warning(
        this, QStringLiteral("Delete objects"),
        keys.size() == 1
            ? QStringLiteral("Delete \"%1\"?\n\nThis cannot be undone.").arg(keys.first())
            : QStringLiteral("Delete %1 objects?\n\nThis cannot be undone.\n\n%2")
                  .arg(keys.size())
                  .arg(elide(keys.mid(0, 8).join(QStringLiteral("\n")), 400)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        return;
    }

    // Deletions are issued together and counted, rather than one modal dialog
    // per failure the way the original did.
    struct Counter {
        int remaining = 0;
        int failed = 0;
        QStringList errors;
    };
    auto *counter = new Counter;
    counter->remaining = keys.size();

    setStatusMessage(QStringLiteral("Deleting %1 object(s)…").arg(keys.size()));

    for (const QString &key : keys) {
        m_client->deleteObject(key, [this, counter, key](const S3Error &error) {
            if (!error.isEmpty()) {
                counter->failed += 1;
                counter->errors.append(QStringLiteral("%1: %2").arg(key, error.message()));
            }
            counter->remaining -= 1;

            if (counter->remaining > 0) {
                return;
            }

            const int failed = counter->failed;
            const QStringList errors = counter->errors;
            delete counter;

            if (failed == 0) {
                setStatusMessage(QStringLiteral("Deleted."));
            } else {
                setStatusMessage(QStringLiteral("%1 of the deletions failed.").arg(failed), true);
                QMessageBox::warning(this, QStringLiteral("Delete"),
                                     errors.join(QLatin1Char('\n')));
            }

            // Re-list rather than removing rows locally: a prefix-based listing
            // is what the server considers authoritative, and a partial failure
            // would otherwise leave the table disagreeing with the bucket.
            reload();
        });
    }
}

void MainWindow::onCopyLink() {
    const QStringList keys = m_browser->selectedKeys();
    if (keys.size() != 1) {
        setStatusMessage(keys.isEmpty()
                             ? QStringLiteral("Select an object to copy a link to.")
                             : QStringLiteral("A presigned link covers one object; %1 are "
                                              "selected.")
                                   .arg(keys.size()),
                         true);
        return;
    }

    const Result<QString> url = m_client->presignedUrl(keys.first(), 3600);
    if (!url.ok()) {
        setStatusMessage(url.error.message(), true);
        return;
    }

    QApplication::clipboard()->setText(url.value);
    setStatusMessage(QStringLiteral("Link copied — valid for 1 hour: %1").arg(elide(url.value)));
}

void MainWindow::onCopyKeys() {
    const QStringList keys = m_browser->selectedKeys();
    if (keys.isEmpty()) {
        return;
    }
    QApplication::clipboard()->setText(keys.join(QLatin1Char('\n')));
    setStatusMessage(QStringLiteral("Copied %1 key(s) to the clipboard.").arg(keys.size()));
}

void MainWindow::onContextMenu(const QPoint &pos) {
    const QModelIndex index = m_browser->view()->indexAt(pos);
    const bool isFolder = index.isValid() && index.data(ObjectModel::IsFolderRole).toBool();

    QMenu menu(this);
    if (isFolder) {
        menu.addAction(Theme::icon(Theme::Glyph::FolderOpen), QStringLiteral("Open"),
                       this, [this, index]() {
                           m_browser->navigateTo(index.data(ObjectModel::KeyRole).toString());
                       });
        menu.addAction(Theme::icon(Theme::Glyph::Copy), QStringLiteral("Copy prefix"),
                       this, [this, index]() {
                           QApplication::clipboard()->setText(
                               index.data(ObjectModel::KeyRole).toString());
                       });
        menu.exec(m_browser->view()->viewport()->mapToGlobal(pos));
        return;
    }

    if (!index.isValid()) {
        menu.addAction(m_actRefresh);
        menu.addAction(m_actLoadMore);
        menu.addSeparator();
        menu.addAction(m_actSelectAll);
        menu.exec(m_browser->view()->viewport()->mapToGlobal(pos));
        return;
    }

    menu.addAction(m_actDownload);
    menu.addAction(m_actLink);
    menu.addSeparator();
    menu.addAction(m_actCopyKeys);
    menu.addSeparator();
    menu.addAction(m_actDelete);
    menu.exec(m_browser->view()->viewport()->mapToGlobal(pos));
}

void MainWindow::onManageBuckets() {
    // Administer the connection in use, so the choice carries straight back into
    // the browser instead of into a window that shares no state.
    const S3Config target = m_config;
    if (target.endpoint.isEmpty()) {
        setStatusMessage(QStringLiteral("Open a connection first."), true);
        return;
    }

    auto *manager = new BucketManager(this);
    manager->setAttribute(Qt::WA_DeleteOnClose);
    manager->setConfig(target);

    if (manager->exec() == QDialog::Accepted) {
        m_config.bucket = manager->chosenBucket();
        m_client->configure(m_config);
        applyConfigToUi();
        reload();
    }
}

void MainWindow::onToggleTransfers() {
    const bool on = m_actTransfers->isChecked();
    m_transfers->setVisible(on);
    if (on) {
        m_statusProgress->setVisible(false);
        m_transfers->refresh();
    } else {
        setBusy(m_loading);
    }
}

void MainWindow::onToggleDetails() {
    m_browser->setDetailsVisible(m_actDetails->isChecked());
}

void MainWindow::onTransferFinished(int itemId, bool ok) {
    Q_UNUSED(itemId);

    // A completed upload changes the bucket's contents, so the table would be
    // stale. Reloading a single page is cheap enough and avoids the original's
    // problem of an upload that never appeared in the list.
    if (ok && m_queue->isIdle() && !m_config.bucket.isEmpty()) {
        reload();
    }
}

void MainWindow::onCheckVersion() {
    setStatusMessage(QStringLiteral("Checking for updates…"));

    // A plain GET against the upstream manifest, since that is the only version
    // marker the project publishes. The original hard-coded this same URL inside
    // its UI code; here it is one named constant.
    auto *manager = new QNetworkAccessManager(this);
    connect(manager, &QNetworkAccessManager::finished, this,
            [this, manager](QNetworkReply *reply) {
                reply->deleteLater();
                manager->deleteLater();

                if (reply->error() != QNetworkReply::NoError) {
                    setStatusMessage(QStringLiteral("Could not check for updates: %1")
                                         .arg(reply->errorString()),
                                     true);
                    return;
                }

                // FyneApp.toml is TOML; the version line reads  Version = "0.10.0".
                const QString body = QString::fromUtf8(reply->readAll());
                QString latest;
                for (const QString &line : body.split(QLatin1Char('\n'))) {
                    const QString trimmed = line.trimmed();
                    if (trimmed.startsWith(QStringLiteral("Version"), Qt::CaseInsensitive) &&
                        trimmed.contains(QLatin1Char('='))) {
                        latest = trimmed.section(QLatin1Char('='), 1)
                                     .remove(QLatin1Char('"'))
                                     .remove(QLatin1Char(' '))
                                     .trimmed();
                        break;
                    }
                }

                if (latest.isEmpty()) {
                    setStatusMessage(QStringLiteral("The upstream version could not be read."),
                                     true);
                    return;
                }

                if (latest == QLatin1String(kVersion)) {
                    setStatusMessage(QStringLiteral("Up to date (version %1).")
                                         .arg(QLatin1String(kVersion)));
                    return;
                }

                setStatusMessage(QStringLiteral("Upstream is at %1; this build is %2.")
                                     .arg(latest, QLatin1String(kVersion)));

                const auto answer = QMessageBox::information(
                    this, QStringLiteral("Version"),
                    QStringLiteral("The upstream project reports version %1.\n"
                                   "This build is %2.\n\nOpen the project page?")
                        .arg(latest, QLatin1String(kVersion)),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

                if (answer == QMessageBox::Yes) {
                    QDesktopServices::openUrl(QUrl(QString::fromLatin1(kProjectUrl)));
                }
            });

    manager->get(QNetworkRequest(QUrl(QString::fromLatin1(kVersionUrl))));
}

void MainWindow::onAbout() {
    QMessageBox::about(
        this, QStringLiteral("About US3"),
        QStringLiteral("<h3>US3 %1</h3>"
                       "<p>A Qt front end for S3-compatible object storage, with UCloud US3 "
                       "and other non-AWS providers as first-class targets.</p>"
                       "<p>Written against the S3 REST API with its own SigV4 signer — no "
                       "vendor SDK.</p>"
                       "<p style='color:#616161'>Inspired by <a href=\"%2\">pteich/us3ui</a>, "
                       "which this is a from-scratch rewrite of rather than a port.</p>")
            .arg(QLatin1String(kVersion), QLatin1String(kProjectUrl)));
}

// ---------------------------------------------------------------------------
// Drag and drop
// ---------------------------------------------------------------------------

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (!event->mimeData()->hasUrls()) {
        return;
    }

    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event) {
    const QList<QUrl> urls = event->mimeData()->urls();

    QStringList paths;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = url.toLocalFile();
        if (QFileInfo(path).isFile()) {
            paths.append(path);
        }
    }

    if (paths.isEmpty()) {
        setStatusMessage(QStringLiteral("Only files can be uploaded by dropping."), true);
        return;
    }

    // Dropping before a connection exists is a normal mistake; keep the paths
    // and say what is needed rather than discarding the gesture.
    if (m_config.bucket.isEmpty() || !m_config.validate().isEmpty()) {
        m_pendingDrop = paths;
        setStatusMessage(QStringLiteral("%1 file(s) held — open a connection to upload them.")
                             .arg(paths.size()),
                         true);
        return;
    }

    for (const QString &path : paths) {
        m_queue->enqueueUpload(path, uploadTargetKey(QFileInfo(path).fileName()));
    }

    m_actTransfers->setChecked(true);
    setStatusMessage(QStringLiteral("Queued %1 dropped file(s).").arg(paths.size()));
    event->acceptProposedAction();

    // Anything held from an earlier drop goes too, now that a connection exists.
    if (!m_pendingDrop.isEmpty()) {
        for (const QString &path : m_pendingDrop) {
            m_queue->enqueueUpload(path, uploadTargetKey(QFileInfo(path).fileName()));
        }
        m_pendingDrop.clear();
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    const int active = m_transfers ? [this]() {
        int n = 0;
        for (const TransferItem &item : m_queue->items()) {
            if (item.state == TransferItem::State::Queued ||
                item.state == TransferItem::State::Running) {
                ++n;
            }
        }
        return n;
    }() : 0;

    if (active == 0) {
        event->accept();
        return;
    }

    const auto answer = QMessageBox::question(
        this, QStringLiteral("Transfers in progress"),
        QStringLiteral("%1 transfer(s) are still running. Cancelling them may leave a "
                       "partial download on disk.\n\nQuit anyway?")
            .arg(active),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (answer == QMessageBox::Yes) {
        m_queue->cancelAll();
        m_client->cancelAll();
        event->accept();
    } else {
        event->ignore();
    }
}

} // namespace us3
