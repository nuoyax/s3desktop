#include "core/TransferQueue.h"

#include "core/S3Client.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QTimer>

namespace us3 {

QString TransferItem::displayName() const {
    return direction == Direction::Upload ? QFileInfo(localPath).fileName()
                                          : objectKey.section(QLatin1Char('/'), -1);
}

double TransferItem::fraction() const {
    return bytesTotal > 0 ? static_cast<double>(bytesDone) / static_cast<double>(bytesTotal) : 0.0;
}

TransferQueue::TransferQueue(QObject *parent) : QObject(parent) {}
TransferQueue::~TransferQueue() = default;

void TransferQueue::setClient(S3Client *client) {
    cancelAll();
    m_client = client;
}

void TransferQueue::setConcurrency(int n) {
    m_concurrency = qBound(1, n, 8);
    pump();
}

int TransferQueue::enqueueUpload(const QString &localPath, const QString &objectKey) {
    TransferItem item;
    item.direction = TransferItem::Direction::Upload;
    item.localPath = localPath;
    item.objectKey = objectKey;
    item.bytesTotal = QFileInfo(localPath).size();

    const int id = m_nextId++;
    item.id = id;
    m_items.append(item);
    m_idToIndex.insert(id, m_items.size() - 1);

    pump();
    return id;
}

int TransferQueue::enqueueDownload(const QString &objectKey, const QString &localDir) {
    TransferItem item;
    item.direction = TransferItem::Direction::Download;
    item.objectKey = objectKey;
    item.localPath = QDir(localDir).filePath(objectKey.section(QLatin1Char('/'), -1));

    const int id = m_nextId++;
    item.id = id;
    m_items.append(item);
    m_idToIndex.insert(id, m_items.size() - 1);

    pump();
    return id;
}

bool TransferQueue::isIdle() const {
    for (const TransferItem &item : m_items) {
        if (item.state == TransferItem::State::Queued || item.state == TransferItem::State::Running) {
            return false;
        }
    }
    return true;
}

void TransferQueue::pump() {
    if (!m_client) {
        return;
    }

    int running = 0;
    for (const TransferItem &item : m_items) {
        if (item.state == TransferItem::State::Running) {
            ++running;
        }
    }

    for (int i = 0; i < m_items.size() && running < m_concurrency; ++i) {
        if (m_items[i].state != TransferItem::State::Queued) {
            continue;
        }
        if (m_items[i].direction == TransferItem::Direction::Upload) {
            startUpload(i);
        } else {
            startDownload(i);
        }
        ++running;
    }
}

void TransferQueue::startUpload(int index) {
    m_items[index].state = TransferItem::State::Running;
    m_items[index].attempt += 1;
    m_items[index].bytesDone = 0;

    const QString localPath = m_items[index].localPath;
    const QString objectKey = m_items[index].objectKey;

    const int requestId = m_client->uploadFile(
        localPath, objectKey,
        [this, index](qint64 done, qint64 total) {
            if (index >= m_items.size()) {
                return;
            }
            m_items[index].bytesDone = done;
            m_items[index].bytesTotal = total;
            emit changed();
        },
        [this, index](const S3Error &error) {
            if (index >= m_items.size()) {
                return;
            }
            completeItem(index, error);
        });

    m_requestIds.insert(index, requestId);
    emit changed();
}

void TransferQueue::startDownload(int index) {
    m_items[index].state = TransferItem::State::Running;
    m_items[index].attempt += 1;
    m_items[index].bytesDone = 0;

    const QString localPath = m_items[index].localPath;

    // Open the destination before the request so a permissions problem is
    // reported as such rather than after pulling the whole object down.
    auto *out = new QFile(localPath);
    if (!out->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const S3Error error = S3Error(ErrorKind::Config,
                                      QStringLiteral("Cannot write %1: %2")
                                          .arg(QFileInfo(localPath).fileName(), out->errorString()));
        out->deleteLater();
        completeItem(index, error);
        return;
    }

    const int requestId = m_client->downloadObject(
        m_items[index].objectKey,
        [this, index, out](bool ok, QNetworkReply *reply, const S3Error &error) {
            if (index >= m_items.size()) {
                out->close();
                out->deleteLater();
                if (reply) {
                    reply->deleteLater();
                }
                return;
            }

            if (!ok || !reply) {
                out->close();
                // Remove the partial file so a retry starts clean and a failed
                // download leaves nothing behind.
                out->remove();
                out->deleteLater();
                completeItem(index, error);
                return;
            }

            connect(reply, &QNetworkReply::downloadProgress, this,
                    [this, index](qint64 done, qint64 total) {
                        if (index >= m_items.size()) {
                            return;
                        }
                        m_items[index].bytesDone = done;
                        m_items[index].bytesTotal = total;
                        emit changed();
                    });

            connect(reply, &QIODevice::readyRead, this, [out, reply]() {
                out->write(reply->readAll());
            });

            connect(reply, &QNetworkReply::finished, this, [this, index, out, reply]() {
                out->write(reply->readAll());
                out->close();
                out->deleteLater();
                reply->deleteLater();

                if (index >= m_items.size()) {
                    return;
                }

                if (reply->error() == QNetworkReply::NoError) {
                    completeItem(index, {});
                } else if (reply->error() == QNetworkReply::OperationCanceledError) {
                    completeItem(index, S3Error::cancelled());
                } else {
                    const int status =
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    completeItem(index, status > 0 ? S3Error::fromResponse(status, {})
                                                   : S3Error::fromNetwork(
                                                         static_cast<int>(reply->error()),
                                                         reply->errorString()));
                }
            });
        });

    m_requestIds.insert(index, requestId);
    emit changed();
}

bool TransferQueue::shouldRetry(const TransferItem &item) const {
    return item.error.isRetryable() && item.attempt < item.maxAttempts;
}

void TransferQueue::completeItem(int index, const S3Error &error) {
    if (index >= m_items.size()) {
        return;
    }

    m_requestIds.remove(index);

    TransferItem &item = m_items[index];

    if (!error.isEmpty()) {
        item.error = error;

        if (error.kind() == ErrorKind::Cancelled) {
            item.state = TransferItem::State::Cancelled;
            emit changed();
            emit finished(index, false);
            pump();
            return;
        }

        if (shouldRetry(item)) {
            // Exponential backoff, capped, so a throttling provider gets room to
            // recover without the UI appearing to hang.
            const int delayMs = qMin(8000, 500 * (1 << (item.attempt - 1)));
            item.state = TransferItem::State::Queued;
            emit changed();

            QTimer::singleShot(delayMs, this, [this]() { pump(); });
            return;
        }

        item.state = TransferItem::State::Failed;
        emit changed();
        emit finished(index, false);
        pump();
        return;
    }

    item.state = TransferItem::State::Done;
    item.bytesDone = item.bytesTotal;
    item.error = {};
    emit changed();
    emit finished(index, true);
    pump();
}

void TransferQueue::cancel(int itemId) {
    const int index = m_idToIndex.value(itemId, -1);
    if (index < 0 || index >= m_items.size()) {
        return;
    }

    TransferItem &item = m_items[index];

    if (item.state == TransferItem::State::Queued) {
        item.state = TransferItem::State::Cancelled;
        emit changed();
        emit finished(index, false);
        return;
    }

    if (item.state == TransferItem::State::Running) {
        if (m_client) {
            m_client->cancel(m_requestIds.value(index, 0));
        }
        // The reply's finished handler calls completeItem, which sets the
        // terminal state; nothing more to do here.
    }
}

void TransferQueue::cancelAll() {
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].state == TransferItem::State::Queued) {
            m_items[i].state = TransferItem::State::Cancelled;
        }
    }
    if (m_client) {
        m_client->cancelAll();
    }
    emit changed();
}

void TransferQueue::clearFinished() {
    QList<TransferItem> kept;
    QHash<int, int> newIndex;
    for (auto it = m_idToIndex.constBegin(); it != m_idToIndex.constEnd(); ++it) {
        const int index = it.value();
        if (index < 0 || index >= m_items.size()) {
            continue;
        }
        const TransferItem &item = m_items[index];
        const bool terminal = item.state == TransferItem::State::Done ||
                              item.state == TransferItem::State::Failed ||
                              item.state == TransferItem::State::Cancelled;
        if (!terminal) {
            newIndex.insert(it.key(), kept.size());
            kept.append(item);
        }
    }
    m_items = kept;
    m_idToIndex = newIndex;
    m_requestIds.clear();
    emit changed();
}

} // namespace us3
