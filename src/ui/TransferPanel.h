#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;

namespace us3 {

class TransferQueue;

/// The transfer list, shown in a dock at the bottom of the window.
///
/// The original had no transfer view at all: uploads ran one at a time behind a
/// single progress bar, and neither an upload nor a download could be stopped.
/// This panel is the surface for the queue's state — per-item progress, retry
/// counts, errors, and a cancel button that works on both.
class TransferPanel : public QWidget {
    Q_OBJECT

public:
    explicit TransferPanel(TransferQueue *queue, QWidget *parent = nullptr);

    /// Number of items still running or queued.
    int activeCount() const;

signals:
    void closeRequested();

public slots:
    /// Re-read the queue and repaint. Public so the window can force a
    /// refresh after toggling the panel visible.
    void refresh();

private slots:
    void onCancelSelected();
    void onClearFinished();

private:
    void buildUi();

    TransferQueue *m_queue = nullptr;

    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_clearButton = nullptr;

    /// Coalesces bursts of progress signals. Uploading a large file emits
    /// progress far faster than a table can usefully repaint, and repainting on
    /// every one of them is what made the original's UI stall during a upload.
    QTimer *m_refreshTimer = nullptr;
    bool m_refreshPending = false;
};

} // namespace us3
