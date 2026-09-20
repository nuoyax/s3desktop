#include "ui/BucketManager.h"

#include "core/S3Client.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

namespace s3desktop {

BucketManager::BucketManager(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Buckets"));
    setMinimumSize(560, 500);
    buildUi();
}

void BucketManager::buildUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *caption = new QLabel(QStringLiteral("Buckets available to this connection"));
    caption->setObjectName(QStringLiteral("panelSection"));
    layout->addWidget(caption);

    m_table = new QTableWidget(0, 2);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Bucket"), QStringLiteral("Created")});
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(Theme::rowHeight);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->setColumnWidth(1, 160);
    connect(m_table, &QTableWidget::itemDoubleClicked, this, &BucketManager::onUseSelected);
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            [this]() { m_deleteButton->setEnabled(!m_table->selectedItems().isEmpty()); });
    layout->addWidget(m_table, 1);

    auto *createRow = new QHBoxLayout;
    createRow->setSpacing(6);

    m_newBucket = new QLineEdit;
    m_newBucket->setPlaceholderText(QStringLiteral("new bucket name"));
    connect(m_newBucket, &QLineEdit::returnPressed, this, &BucketManager::onCreate);
    connect(m_newBucket, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_createButton->setEnabled(!text.trimmed().isEmpty());
    });
    createRow->addWidget(m_newBucket, 1);

    m_createButton = new QPushButton(QStringLiteral("Create"));
    m_createButton->setEnabled(false);
    connect(m_createButton, &QPushButton::clicked, this, &BucketManager::onCreate);
    createRow->addWidget(m_createButton);

    layout->addLayout(createRow);

    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("hint"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);

    m_refreshButton = new QPushButton(QStringLiteral("Refresh"));
    connect(m_refreshButton, &QPushButton::clicked, this, &BucketManager::onRefresh);
    buttons->addWidget(m_refreshButton);

    m_deleteButton = new QPushButton(QStringLiteral("Delete"));
    m_deleteButton->setEnabled(false);
    connect(m_deleteButton, &QPushButton::clicked, this, &BucketManager::onDelete);
    buttons->addWidget(m_deleteButton);

    buttons->addStretch(1);

    auto *close = new QPushButton(QStringLiteral("Close"));
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(close);

    m_useButton = new QPushButton(QStringLiteral("Use selected"));
    m_useButton->setDefault(true);
    connect(m_useButton, &QPushButton::clicked, this, &BucketManager::onUseSelected);
    buttons->addWidget(m_useButton);

    layout->addLayout(buttons);
}

void BucketManager::setConfig(const S3Config &config) {
    m_config = config;

    if (!m_config.validate().isEmpty()) {        setStatus(QStringLiteral("This connection is not complete enough to list buckets. "
                                 "Fill in the endpoint and access key first."),
                  true);
        return;
    }

    setBusy(false);
    onRefresh();
}

void BucketManager::setBusy(bool busy) {
    m_refreshButton->setEnabled(!busy);
    m_createButton->setEnabled(!busy && !m_newBucket->text().trimmed().isEmpty());
    m_deleteButton->setEnabled(!busy && !m_table->selectedItems().isEmpty());
}

void BucketManager::setStatus(const QString &text, bool isError) {
    m_status->setText(text);
    m_status->setObjectName(isError ? QStringLiteral("errLabel") : QStringLiteral("hint"));
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
}

void BucketManager::onRefresh() {
    setStatus(QStringLiteral("Listing buckets…"), false);
    setBusy(true);

    // A dedicated client: this dialog can outlive a connection change, and its
    // requests must not be cancelled by the main window's cancelAll.
    auto *client = new S3Client(this);
    client->configure(m_config);

    client->listBuckets([this, client](const Result<QList<BucketInfo>> &result) {
        client->deleteLater();
        setBusy(false);

        if (!result.ok()) {
            refreshTable({});
            setStatus(result.error.message(), true);
            return;
        }

        refreshTable(result.value);
        setStatus(QStringLiteral("%1 bucket(s).").arg(result.value.size()), false);
    });
}

void BucketManager::refreshTable(const QList<BucketInfo> &buckets) {
    m_table->setRowCount(0);
    m_table->setRowCount(buckets.size());

    for (int i = 0; i < buckets.size(); ++i) {
        const BucketInfo &bucket = buckets.at(i);
        m_table->setItem(i, 0, new QTableWidgetItem(bucket.name));
        m_table->setItem(i, 1,
                         new QTableWidgetItem(bucket.creationDate.isValid()
                                                  ? bucket.creationDate.toString(
                                                        QStringLiteral("yyyy-MM-dd HH:mm"))
                                                  : QStringLiteral("—")));
    }

    setBusy(false);
}

void BucketManager::onCreate() {
    const QString name = m_newBucket->text().trimmed();
    if (name.isEmpty()) {
        return;
    }

    // Bucket naming rules that every S3 implementation enforces; catching them
    // here turns a server-side 400 into an immediate, specific message.
    if (name.size() < 3 || name.size() > 63) {
        setStatus(QStringLiteral("Bucket names must be 3 to 63 characters."), true);
        return;
    }
    if (name.contains(QRegularExpression(QStringLiteral("[^a-z0-9.\\-]")))) {
        setStatus(QStringLiteral("Bucket names may only contain lowercase letters, digits, "
                                 "dots and hyphens."),
                  true);
        return;
    }
    if (name.startsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char('.')) ||
        name.startsWith(QLatin1String("xn--")) || name.contains(QLatin1String(".."))) {
        setStatus(QStringLiteral("That bucket name is not valid for S3."), true);
        return;
    }

    setBusy(true);
    setStatus(QStringLiteral("Creating %1…").arg(name), false);

    auto *client = new S3Client(this);
    client->configure(m_config);

    const QString region = client->effectiveRegion();
    client->createBucket(name, region, [this, client, name](const S3Error &error) {
        client->deleteLater();

        if (!error.isEmpty()) {
            setBusy(false);
            setStatus(error.message(), true);
            return;
        }

        m_newBucket->clear();
        setStatus(QStringLiteral("Created %1.").arg(name), false);
        onRefresh();
    });
}

void BucketManager::onDelete() {
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0)) {
        return;
    }

    const QString name = m_table->item(row, 0)->text();

    const auto answer = QMessageBox::warning(
        this, QStringLiteral("Delete bucket"),
        QStringLiteral("Delete the bucket \"%1\"?\n\nS3 only allows this when the bucket is "
                       "empty. This cannot be undone.")
            .arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        return;
    }

    setBusy(true);
    setStatus(QStringLiteral("Deleting %1…").arg(name), false);

    auto *client = new S3Client(this);
    client->configure(m_config);

    client->deleteBucket(name, [this, client, name](const S3Error &error) {
        client->deleteLater();

        if (!error.isEmpty()) {
            setBusy(false);
            setStatus(error.message(), true);
            return;
        }

        setStatus(QStringLiteral("Deleted %1.").arg(name), false);
        onRefresh();
    });
}

void BucketManager::onUseSelected() {
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0)) {
        setStatus(QStringLiteral("Select a bucket first."), true);
        return;
    }

    m_chosen = m_table->item(row, 0)->text();
    accept();
}

} // namespace s3desktop
