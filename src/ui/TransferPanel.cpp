#include "ui/TransferPanel.h"

#include "core/TransferQueue.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace us3 {

namespace {

constexpr int kProgressColumn = 2;
constexpr int kStatusColumn = 3;

/// Paints the progress column as a bar with the percentage over it.
///
/// A delegate rather than a QProgressBar cell widget: a widget per row means
/// creating and destroying them on every refresh, and with the refresh running
/// several times a second during an upload that is both slow and flickery.
class ProgressDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        const double fraction = index.data(Qt::UserRole).toDouble();
        const bool indeterminate = index.data(Qt::UserRole + 1).toBool();
        const QColor barColour = index.data(Qt::UserRole + 2).value<QColor>();
        const QString caption = index.data(Qt::DisplayRole).toString();

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        if (option.state & QStyle::State_Selected) {
            painter->fillRect(option.rect, Theme::accentSoft());
        }

        QRect track = option.rect.adjusted(6, 0, -6, 0);
        track.setHeight(6);
        track.moveTop(option.rect.center().y() - 3);

        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0xEB, 0xEB, 0xEB));
        painter->drawRoundedRect(track, 3, 3);

        if (fraction > 0.0 || !indeterminate) {
            QRect fill = track;
            fill.setWidth(static_cast<int>(track.width() * qBound(0.0, fraction, 1.0)));
            if (fill.width() > 0) {
                painter->setBrush(barColour.isValid() ? barColour : Theme::accent());
                painter->drawRoundedRect(fill, 3, 3);
            }
        }

        // The caption sits under the bar in the same cell, so a row shows both
        // the bar and the exact byte counts without a second column.
        QRect textRect = option.rect;
        textRect.setTop(track.bottom() + 2);
        painter->setPen(Theme::textMuted());
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, caption);

        painter->restore();
    }
};

QString stateColour(const TransferItem &item) {
    switch (item.state) {
    case TransferItem::State::Done:
        return QStringLiteral("");
    case TransferItem::State::Failed:
        return QStringLiteral("errLabel");
    case TransferItem::State::Cancelled:
        return QStringLiteral("hint");
    default:
        return QString();
    }
}

QString stateText(const TransferItem &item) {
    switch (item.state) {
    case TransferItem::State::Queued:
        return item.attempt > 1 ? QStringLiteral("Retrying (attempt %1 of %2)")
                                      .arg(item.attempt + 1)
                                      .arg(item.maxAttempts)
                                : QStringLiteral("Queued");
    case TransferItem::State::Running:
        return item.attempt > 1 ? QStringLiteral("Running (attempt %1)").arg(item.attempt)
                                : QStringLiteral("Running");
    case TransferItem::State::Done:
        return QStringLiteral("Done");
    case TransferItem::State::Cancelled:
        return QStringLiteral("Cancelled");
    case TransferItem::State::Failed:
        return item.error.message().isEmpty() ? QStringLiteral("Failed") : item.error.message();
    }
    return {};
}

} // namespace

TransferPanel::TransferPanel(TransferQueue *queue, QWidget *parent)
    : QWidget(parent), m_queue(queue) {
    buildUi();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(120);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        m_refreshPending = false;
        refresh();
    });

    connect(m_queue, &TransferQueue::changed, this, [this]() {
        if (m_refreshPending) {
            return;
        }
        m_refreshPending = true;
        m_refreshTimer->start();
    });

    refresh();
}

void TransferPanel::buildUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget;
    header->setObjectName(QStringLiteral("sidePanelHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(10, 5, 8, 5);
    headerLayout->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("Transfers"));
    title->setObjectName(QStringLiteral("panelSection"));
    headerLayout->addWidget(title);

    m_summary = new QLabel(QStringLiteral("idle"));
    m_summary->setObjectName(QStringLiteral("hint"));
    headerLayout->addWidget(m_summary);

    headerLayout->addStretch(1);

    m_clearButton = new QPushButton(QStringLiteral("Clear finished"));
    m_clearButton->setObjectName(QStringLiteral("link"));
    m_clearButton->setCursor(Qt::PointingHandCursor);
    connect(m_clearButton, &QPushButton::clicked, this, &TransferPanel::onClearFinished);
    headerLayout->addWidget(m_clearButton);

    m_cancelButton = new QPushButton(QStringLiteral("Cancel selected"));
    m_cancelButton->setObjectName(QStringLiteral("link"));
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    connect(m_cancelButton, &QPushButton::clicked, this, &TransferPanel::onCancelSelected);
    headerLayout->addWidget(m_cancelButton);

    auto *close = new QPushButton(QStringLiteral("Hide"));
    close->setObjectName(QStringLiteral("link"));
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QPushButton::clicked, this, &TransferPanel::closeRequested);
    headerLayout->addWidget(close);

    layout->addWidget(header);

    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("Item"), QStringLiteral("Type"), QStringLiteral("Progress"),
         QStringLiteral("Status")});
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(34);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_table->setColumnWidth(1, 80);
    m_table->setColumnWidth(2, 220);
    m_table->setColumnWidth(3, 300);
    m_table->setItemDelegateForColumn(kProgressColumn, new ProgressDelegate(this));

    // Double-clicking a row cancels it, which is the fastest way to stop the
    // one transfer the user actually cares about.
    connect(m_table, &QTableWidget::itemDoubleClicked, this, &TransferPanel::onCancelSelected);

    layout->addWidget(m_table, 1);
}

int TransferPanel::activeCount() const {
    int n = 0;
    for (const TransferItem &item : m_queue->items()) {
        if (item.state == TransferItem::State::Queued || item.state == TransferItem::State::Running) {
            ++n;
        }
    }
    return n;
}

void TransferPanel::refresh() {
    const QList<TransferItem> items = m_queue->items();

    // Rebuild in place: setting the row count to the same value keeps the
    // current selection, which is what makes "Cancel selected" usable while a
    // transfer is progressing underneath it.
    m_table->setRowCount(items.size());

    for (int i = 0; i < items.size(); ++i) {
        const TransferItem &item = items.at(i);

        auto *nameItem = m_table->item(i, 0);
        if (!nameItem) {
            nameItem = new QTableWidgetItem;
            m_table->setItem(i, 0, nameItem);
        }
        nameItem->setText(item.displayName());
        nameItem->setToolTip(item.direction == TransferItem::Direction::Upload
                                 ? QStringLiteral("%1  →  %2").arg(item.localPath, item.objectKey)
                                 : QStringLiteral("%1  →  %2").arg(item.objectKey, item.localPath));
        nameItem->setData(Qt::UserRole, item.id);

        auto *typeItem = m_table->item(i, 1);
        if (!typeItem) {
            typeItem = new QTableWidgetItem;
            m_table->setItem(i, 1, typeItem);
        }
        typeItem->setText(item.direction == TransferItem::Direction::Upload
                              ? QStringLiteral("Upload")
                              : QStringLiteral("Download"));

        auto *progressItem = m_table->item(i, kProgressColumn);
        if (!progressItem) {
            progressItem = new QTableWidgetItem;
            m_table->setItem(i, kProgressColumn, progressItem);
        }
        const bool indeterminate = item.bytesTotal <= 0 && item.state == TransferItem::State::Running;
        progressItem->setText(item.bytesTotal > 0
                                  ? QStringLiteral("%1 of %2  (%3%)")
                                        .arg(Theme::formatBytes(item.bytesDone),
                                             Theme::formatBytes(item.bytesTotal))
                                        .arg(static_cast<int>(item.fraction() * 100))
                                  : QStringLiteral("—"));
        progressItem->setData(Qt::UserRole, item.fraction());
        progressItem->setData(Qt::UserRole + 1, indeterminate);
        progressItem->setData(Qt::UserRole + 2,
                              item.state == TransferItem::State::Failed ? Theme::danger()
                                                                        : Theme::accent());

        auto *statusItem = m_table->item(i, kStatusColumn);
        if (!statusItem) {
            statusItem = new QTableWidgetItem;
            m_table->setItem(i, kStatusColumn, statusItem);
        }
        statusItem->setText(stateText(item));
        statusItem->setToolTip(item.error.isEmpty() ? QString() : item.error.toString());
        const QString colourName = stateColour(item);
        if (!colourName.isEmpty()) {
            statusItem->setForeground(colourName == QStringLiteral("errLabel") ? Theme::danger()
                                                                               : Theme::textFaint());
        } else {
            statusItem->setForeground(Theme::text());
        }
    }

    const int active = activeCount();
    int done = 0;
    int failed = 0;
    for (const TransferItem &item : items) {
        if (item.state == TransferItem::State::Done) {
            ++done;
        } else if (item.state == TransferItem::State::Failed) {
            ++failed;
        }
    }

    QString summary;
    if (active > 0) {
        summary = QStringLiteral("%1 active").arg(active);
    } else {
        summary = QStringLiteral("idle");
    }
    if (done > 0) {
        summary += QStringLiteral(" · %1 done").arg(done);
    }
    if (failed > 0) {
        summary += QStringLiteral(" · %1 failed").arg(failed);
    }
    m_summary->setText(summary);

    m_cancelButton->setEnabled(active > 0);
    m_clearButton->setEnabled(done + failed > 0);
}

void TransferPanel::onCancelSelected() {
    QList<QTableWidgetItem *> selected = m_table->selectedItems();
    if (selected.isEmpty()) {
        return;
    }

    // One cancel per row: selectedItems() returns every cell, so collect the
    // distinct ids first.
    QSet<int> ids;
    for (QTableWidgetItem *item : selected) {
        const int id = m_table->item(item->row(), 0)->data(Qt::UserRole).toInt();
        if (id > 0) {
            ids.insert(id);
        }
    }

    for (int id : ids) {
        m_queue->cancel(id);
    }
}

void TransferPanel::onClearFinished() {
    m_queue->clearFinished();
    refresh();
}

} // namespace us3
