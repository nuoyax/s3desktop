#pragma once

#include "core/S3Error.h"
#include "core/S3Types.h"

#include <QList>
#include <QObject>
#include <QString>

#include <functional>

namespace s3desktop {

/// A single transfer the user has asked for.
struct TransferItem {
    enum class Direction { Upload, Download };

    /// Stable handle for this item, assigned on enqueue. Carried on the item
    /// itself so a UI rendering a snapshot can call cancel() without keeping a
    /// parallel index.
    int id = 0;

    Direction direction = Direction::Upload;
    QString objectKey;   ///< S3 side
    QString localPath;   ///< filesystem side
    qint64 bytesTotal = 0;
    qint64 bytesDone = 0;

    /// Bumped on each retry, so the UI can show "attempt 2 of 3".
    int attempt = 0;
    int maxAttempts = 3;

    S3Error error;
    enum class State { Queued, Running, Done, Failed, Cancelled };
    State state = State::Queued;

    QString displayName() const;
    double fraction() const;
};

/// Runs uploads and downloads with bounded concurrency, progress reporting,
/// per-item cancellation and retry with backoff.
///
/// The original app uploaded dropped files strictly one at a time and offered no
/// way to abort either transfers or a large upload; a single failure aborted the
/// whole batch. Concurrency, cancellation and retry are handled here so the UI
/// only observes state.
class TransferQueue : public QObject {
    Q_OBJECT

public:
    explicit TransferQueue(QObject *parent = nullptr);
    ~TransferQueue() override;

    /// Point the queue at a client. The client must outlive the queue.
    /// Any in-flight work is cancelled.
    void setClient(class S3Client *client);

    /// How many transfers run at once. Clamped to 1..8.
    void setConcurrency(int n);
    int concurrency() const { return m_concurrency; }

    /// Enqueue an upload of a local file to `objectKey`.
    /// Returns the item id.
    int enqueueUpload(const QString &localPath, const QString &objectKey);

    /// Enqueue a download of `objectKey` into `localDir`.
    int enqueueDownload(const QString &objectKey, const QString &localDir);

    /// Cancel one item. A queued item is dropped; a running one is aborted.
    void cancel(int itemId);

    /// Cancel everything, including the queued backlog.
    void cancelAll();

    /// Clear finished items from the list.
    void clearFinished();

    /// A snapshot for rendering. Copies rather than a live reference so the UI
    /// cannot observe the queue mid-mutation.
    QList<TransferItem> items() const { return m_items; }

    bool isIdle() const;

signals:
    /// Emitted whenever any item's state, progress or error changes.
    void changed();

    /// Emitted once per item when it reaches a terminal state.
    void finished(int itemId, bool ok);

private:
    void pump();
    void startUpload(int index);
    void startDownload(int index);
    void completeItem(int index, const S3Error &error);
    bool shouldRetry(const TransferItem &item) const;

    S3Client *m_client = nullptr;
    QList<TransferItem> m_items;
    int m_concurrency = 3;
    int m_nextId = 1;
    QHash<int, int> m_idToIndex; ///< item id -> index in m_items
    QHash<int, int> m_requestIds; ///< item index -> live S3Client request id
};

} // namespace s3desktop
