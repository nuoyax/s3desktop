#include "ui/ConnectDialog.h"

#include "compat/TargetProfile.h"
#include "core/ConnectionStore.h"
#include "core/CredentialStore.h"
#include "core/Log.h"
#include "core/S3Client.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QVBoxLayout>

namespace s3desktop {

ConnectDialog::ConnectDialog(ConnectionStore *store, S3Client *client, QWidget *parent)
    : QDialog(parent), m_store(store), m_client(client) {
    setWindowTitle(QStringLiteral("Connections"));
    setMinimumSize(880, 560);
    buildUi();
    refreshList();

    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    } else {
        loadIntoForm(S3Config{});
    }
}

void ConnectDialog::buildUi() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(10);

    auto *split = new QSplitter(Qt::Horizontal);
    split->addWidget(buildListPane());
    split->addWidget(buildFormPane());
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({240, 620});
    outer->addWidget(split, 1);

    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("hint"));
    m_status->setWordWrap(true);
    outer->addWidget(m_status);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);

    m_testButton = new QPushButton(QStringLiteral("Test connection"));
    connect(m_testButton, &QPushButton::clicked, this, &ConnectDialog::onTest);
    buttons->addWidget(m_testButton);

    buttons->addStretch(1);

    auto *save = new QPushButton(QStringLiteral("Save"));
    connect(save, &QPushButton::clicked, this, &ConnectDialog::onSave);
    buttons->addWidget(save);

    auto *cancel = new QPushButton(QStringLiteral("Cancel"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);

    auto *connectButton = new QPushButton(QStringLiteral("Connect"));
    connectButton->setDefault(true);
    connect(connectButton, &QPushButton::clicked, this, &ConnectDialog::onAccept);
    buttons->addWidget(connectButton);

    outer->addLayout(buttons);

    // Any edit marks the form dirty so the user is warned before losing it.
    connect(m_name, &QLineEdit::textEdited, this, &ConnectDialog::onFormEdited);
    connect(m_endpoint, &QLineEdit::textEdited, this, [this]() {
        // Mirror a scheme typed into the endpoint into the Transport combo before
        // anything else reacts to the edit. See syncTransportToEndpointScheme.
        syncTransportToEndpointScheme();
        onFormEdited();
    });
    connect(m_accessKey, &QLineEdit::textEdited, this, &ConnectDialog::onFormEdited);
    connect(m_secretKey, &QLineEdit::textEdited, this, &ConnectDialog::onFormEdited);
    connect(m_bucket, &QLineEdit::textEdited, this, &ConnectDialog::onFormEdited);
    connect(m_prefix, &QLineEdit::textEdited, this, &ConnectDialog::onFormEdited);
}

QWidget *ConnectDialog::buildListPane() {
    auto *pane = new QWidget;
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 8, 0);
    layout->setSpacing(6);

    auto *caption = new QLabel(QStringLiteral("Saved connections"));
    caption->setObjectName(QStringLiteral("panelSection"));
    layout->addWidget(caption);

    m_list = new QListWidget;
    m_list->setAlternatingRowColors(false);
    connect(m_list, &QListWidget::currentRowChanged, this, &ConnectDialog::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this]() { onAccept(); });
    layout->addWidget(m_list, 1);

    auto *tools = new QHBoxLayout;
    tools->setSpacing(4);

    auto *add = new QPushButton(QStringLiteral("Add"));
    connect(add, &QPushButton::clicked, this, &ConnectDialog::onAdd);
    tools->addWidget(add);

    m_copyButton = new QPushButton(QStringLiteral("Copy"));
    connect(m_copyButton, &QPushButton::clicked, this, &ConnectDialog::onCopy);
    tools->addWidget(m_copyButton);

    m_deleteButton = new QPushButton(QStringLiteral("Delete"));
    connect(m_deleteButton, &QPushButton::clicked, this, &ConnectDialog::onDelete);
    tools->addWidget(m_deleteButton);

    layout->addLayout(tools);
    return pane;
}

QWidget *ConnectDialog::buildFormPane() {
    auto *pane = new QWidget;
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *group = new QGroupBox(QStringLiteral("Connection"));
    auto *form = new QFormLayout(group);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);

    m_name = new QLineEdit;
    m_name->setPlaceholderText(QStringLiteral("e.g. production"));
    form->addRow(QStringLiteral("Name"), m_name);

    m_target = new QComboBox;
    for (const TargetProfile &profile : TargetProfile::all()) {
        m_target->addItem(profile.displayName, profile.id);
    }
    connect(m_target, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        const TargetProfile profile = TargetProfile::byId(m_target->currentData().toString());
        m_endpointHint->setText(profile.endpointHint);
        m_regionHint->setText(profile.regionHint);
        onFormEdited();
    });
    form->addRow(QStringLiteral("Provider"), m_target);

    m_endpoint = new QLineEdit;
    m_endpoint->setPlaceholderText(QStringLiteral("s3.example.com, 10.0.0.5:9000, or "
                                                  "http://10.0.0.5:9000"));
    form->addRow(QStringLiteral("Endpoint"), m_endpoint);

    m_endpointHint = new QLabel;
    m_endpointHint->setObjectName(QStringLiteral("hint"));
    m_endpointHint->setWordWrap(true);
    form->addRow(QString(), m_endpointHint);

    m_accessKey = new QLineEdit;
    form->addRow(QStringLiteral("Access key"), m_accessKey);

    m_secretKey = new QLineEdit;
    m_secretKey->setEchoMode(QLineEdit::Password);
    m_secretKey->setPlaceholderText(QStringLiteral("stored %1")
                                        .arg(m_store->credentials()->backendName()));
    form->addRow(QStringLiteral("Secret key"), m_secretKey);

    m_bucket = new QLineEdit;
    m_bucket->setPlaceholderText(QStringLiteral("leave empty to list buckets on connect"));
    form->addRow(QStringLiteral("Bucket"), m_bucket);

    auto *bucketRow = new QWidget;
    auto *bucketLayout = new QHBoxLayout(bucketRow);
    bucketLayout->setContentsMargins(0, 0, 0, 0);
    bucketLayout->setSpacing(8);
    bucketLayout->addWidget(m_bucket, 1);
    auto *manage = new QPushButton(QStringLiteral("Manage buckets…"));
    connect(manage, &QPushButton::clicked, this,
            [this]() { emit manageBucketsRequested(readForm()); });
    bucketLayout->addWidget(manage);
    form->addRow(QString(), bucketRow);

    m_region = new QLineEdit;
    m_region->setPlaceholderText(QStringLiteral("empty = derive from endpoint"));
    form->addRow(QStringLiteral("Region"), m_region);

    m_regionHint = new QLabel;
    m_regionHint->setObjectName(QStringLiteral("hint"));
    m_regionHint->setWordWrap(true);
    form->addRow(QString(), m_regionHint);

    m_prefix = new QLineEdit;
    m_prefix->setPlaceholderText(QStringLiteral("start browsing at this key prefix"));
    form->addRow(QStringLiteral("Prefix"), m_prefix);

    m_tls = new QComboBox;
    m_tls->addItem(QStringLiteral("HTTPS, verify certificate"), int(TlsPolicy::VerifyStrict));
    m_tls->addItem(QStringLiteral("HTTPS, accept self-signed"), int(TlsPolicy::AllowSelfSigned));
    m_tls->addItem(QStringLiteral("HTTP (no TLS)"), int(TlsPolicy::Disabled));
    connect(m_tls, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ConnectDialog::onFormEdited);
    form->addRow(QStringLiteral("Transport"), m_tls);

    m_addressing = new QComboBox;
    m_addressing->addItem(QStringLiteral("Path style  (endpoint/bucket/key)"),
                          int(AddressingStyle::PathStyle));
    m_addressing->addItem(QStringLiteral("Virtual host  (bucket.endpoint/key)"),
                          int(AddressingStyle::VirtualHost));
    connect(m_addressing, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ConnectDialog::onFormEdited);
    form->addRow(QStringLiteral("Addressing"), m_addressing);

    auto *note = new QLabel(QStringLiteral(
        "Secrets are encrypted with %1 and kept out of settings.json.\n"
        "A scheme in the endpoint (http:// or https://) overrides the Transport "
        "setting.")
                                .arg(m_store->credentials()->backendName()));
    note->setObjectName(QStringLiteral("hint"));
    note->setWordWrap(true);
    form->addRow(QString(), note);

    layout->addWidget(group);
    layout->addStretch(1);
    return pane;
}

void ConnectDialog::refreshList() {
    const QString keep = m_list->currentItem() ? m_list->currentItem()->text() : QString();

    QSignalBlocker blocker(m_list);
    m_list->clear();

    for (const S3Config &cfg : m_store->connections()) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1\n%2").arg(cfg.name, cfg.endpoint.isEmpty() ? "—" : cfg.endpoint));
        item->setData(Qt::UserRole, cfg.name);
        m_list->addItem(item);
    }

    if (!keep.isEmpty()) {
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(Qt::UserRole).toString() == keep) {
                m_list->setCurrentRow(i);
                break;
            }
        }
    }

    updateButtons();
}

void ConnectDialog::updateButtons() {
    m_deleteButton->setEnabled(m_list->currentRow() >= 0);
    m_copyButton->setEnabled(m_list->currentRow() >= 0);
    // Testing needs a complete configuration, saved or not.
    m_testButton->setEnabled(readForm().validate().isEmpty());
}

void ConnectDialog::onSelectionChanged() {
    QListWidgetItem *item = m_list->currentItem();
    if (!item) {
        updateButtons();
        return;
    }

    const int index = m_store->indexOf(item->data(Qt::UserRole).toString());
    if (index < 0) {
        updateButtons();
        return;
    }

    loadIntoForm(m_store->connections().at(index));
    updateButtons();
}

void ConnectDialog::loadIntoForm(const S3Config &cfg) {
    m_loadingForm = true;

    m_current = cfg;
    m_name->setText(cfg.name);
    m_endpoint->setText(cfg.endpoint);
    m_accessKey->setText(cfg.accessKey);
    m_secretKey->setText(cfg.secretKey);
    m_bucket->setText(cfg.bucket);
    m_region->setText(cfg.region);
    m_prefix->setText(cfg.prefix);

    const QString targetId = cfg.targetId.isEmpty() ? TargetProfile::generic().id
                                                    : cfg.targetId;
    const int targetIndex = m_target->findData(targetId);
    m_target->setCurrentIndex(targetIndex >= 0 ? targetIndex : 0);

    const int tlsIndex = m_tls->findData(int(cfg.tls));
    m_tls->setCurrentIndex(tlsIndex >= 0 ? tlsIndex : 0);

    const int addressingIndex = m_addressing->findData(cfg.addressingStyle);
    m_addressing->setCurrentIndex(addressingIndex >= 0 ? addressingIndex : 0);

    const TargetProfile profile = TargetProfile::byId(m_target->currentData().toString());
    m_endpointHint->setText(profile.endpointHint);
    m_regionHint->setText(profile.regionHint);

    m_loadingForm = false;
    m_dirty = false;
}

S3Config ConnectDialog::readForm() const {
    S3Config cfg;
    cfg.name = m_name->text().trimmed();
    cfg.endpoint = m_endpoint->text().trimmed();
    cfg.accessKey = m_accessKey->text().trimmed();
    cfg.secretKey = m_secretKey->text();
    cfg.bucket = m_bucket->text().trimmed();
    cfg.region = m_region->text().trimmed();
    cfg.prefix = m_prefix->text().trimmed();
    cfg.targetId = m_target->currentData().toString();
    cfg.addressingStyle = m_addressing->currentData().toInt();
    cfg.tls = static_cast<TlsPolicy>(m_tls->currentData().toInt());
    // The combo is the authority for new saves; useSsl is kept consistent so the
    // file still loads correctly in the original app, which only understands
    // that field.
    cfg.useSsl = cfg.effectiveUseSsl();
    return cfg;
}

void ConnectDialog::onFormEdited() {
    if (m_loadingForm) {
        return;
    }
    m_dirty = true;
    setStatus(QString(), false);
}

void ConnectDialog::syncTransportToEndpointScheme() {
    // A scheme typed into the endpoint field and the Transport combo are two ways
    // of stating the same thing. Letting them disagree produces an https request
    // to a plain-HTTP service, whose failure (a TLS handshake error) names
    // neither field. The typed scheme is the more recent statement, so the combo
    // follows it.
    const QString text = m_endpoint->text().trimmed().toLower();
    const int marker = text.indexOf(QStringLiteral("://"));
    if (marker <= 0) {
        return;
    }

    const QString scheme = text.left(marker);
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        return;
    }

    const TlsPolicy wanted = scheme == QStringLiteral("https")
                                 ? TlsPolicy::VerifyStrict
                                 : TlsPolicy::Disabled;
    const int index = m_tls->findData(int(wanted));
    if (index >= 0 && m_tls->currentIndex() != index) {
        // Not a user edit of the combo, so this must not re-enter onFormEdited.
        const QSignalBlocker blocker(m_tls);
        m_tls->setCurrentIndex(index);
    }
}

void ConnectDialog::setStatus(const QString &text, bool isError) {
    m_status->setText(text);
    m_status->setObjectName(isError ? QStringLiteral("errLabel") : QStringLiteral("hint"));
    // Re-polish so the changed object name picks up its stylesheet rule.
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
}

void ConnectDialog::onAdd() {
    if (m_dirty && !commitForm(nullptr)) {
        return;
    }
    loadIntoForm(S3Config{});
    m_name->setText(m_store->uniqueName(QStringLiteral("New connection")));
    m_list->setCurrentRow(-1);
    m_name->setFocus();
    m_name->selectAll();
    updateButtons();
}

void ConnectDialog::onCopy() {
    QListWidgetItem *item = m_list->currentItem();
    if (!item) {
        return;
    }
    const int index = m_store->indexOf(item->data(Qt::UserRole).toString());
    if (index < 0) {
        return;
    }

    S3Config copy = m_store->connections().at(index);
    copy.name = m_store->uniqueName(copy.name);
    m_store->upsert(copy);

    QString ignored;
    m_store->save(&ignored);
    refreshList();

    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == copy.name) {
            m_list->setCurrentRow(i);
            break;
        }
    }
    setStatus(QStringLiteral("Copied. Edit the name and connect."), false);
}

void ConnectDialog::onDelete() {
    QListWidgetItem *item = m_list->currentItem();
    if (!item) {
        return;
    }

    const QString name = item->data(Qt::UserRole).toString();
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Delete connection"),
        QStringLiteral("Delete \"%1\"?\n\nThe stored secret for this connection is removed "
                       "as well. Objects in the bucket are not affected.")
            .arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        return;
    }

    m_store->removeByName(name);
    QString error;
    if (!m_store->save(&error)) {
        setStatus(QStringLiteral("Could not save settings: %1").arg(error), true);
    }
    refreshList();
    m_list->setCurrentRow(m_list->count() > 0 ? 0 : -1);
}

bool ConnectDialog::commitForm(QString *error) {
    const S3Config cfg = readForm();

    if (cfg.name.isEmpty()) {
        setStatus(QStringLiteral("Give the connection a name before saving."), true);
        m_name->setFocus();
        return false;
    }

    const QString problem = cfg.validate();
    if (!problem.isEmpty()) {
        setStatus(problem, true);
        return false;
    }

    m_store->upsert(cfg);
    if (!m_store->save(error)) {
        return false;
    }

    m_dirty = false;
    m_current = cfg;
    refreshList();
    return true;
}

void ConnectDialog::onSave() {
    QString error;
    if (!commitForm(&error)) {
        if (!error.isEmpty()) {
            setStatus(QStringLiteral("Could not save settings: %1").arg(error), true);
        }
        return;
    }
    setStatus(QStringLiteral("Saved. The secret is stored in %1, not in settings.json.")
                  .arg(m_store->credentials()->backendName()),
              false);
}

void ConnectDialog::onTest() {
    const S3Config cfg = readForm();
    const QString problem = cfg.validate();
    if (!problem.isEmpty()) {
        setStatus(problem, true);
        return;
    }

    setStatus(QStringLiteral("Testing %1…").arg(cfg.endpoint), false);
    m_testButton->setEnabled(false);

    // Log the form as submitted, not as saved: a test that fails because a field
    // was mistyped is the common case, and comparing the two is the whole point.
    Log::write(Log::ui(), 0,
               QStringLiteral("test connection: endpoint=\"%1\" host=%2 port=%3 bucket=\"%4\" "
                              "region=\"%5\" profile=%6 addressing=%7 tls=%8 access-key=%9 "
                              "secret=%10")
                   .arg(cfg.endpoint, cfg.host(),
                        cfg.port().isEmpty() ? QStringLiteral("(default)") : cfg.port(), cfg.bucket,
                        cfg.region,
                        cfg.targetId.isEmpty() ? QStringLiteral("generic") : cfg.targetId,
                        cfg.addressingStyle == int(AddressingStyle::VirtualHost)
                            ? QStringLiteral("virtual-host")
                            : QStringLiteral("path-style"),
                        cfg.effectiveUseSsl() ? QStringLiteral("https") : QStringLiteral("http"),
                        Log::redact(cfg.accessKey), Log::redact(cfg.secretKey)));

    // A private client rather than the window's shared one: the modal dialog
    // spins its own event loop, and mixing these requests into the main
    // client's queue would make a later cancel-by-id ambiguous.
    auto *probe = new S3Client(this);
    probe->configure(cfg);

    probe->listBuckets([this, probe](const Result<QList<BucketInfo>> &result) {
        m_testButton->setEnabled(true);

        if (!result.ok()) {
            QString detail = result.error.message();
            if (!result.error.serverCode().isEmpty()) {
                detail += QStringLiteral("  [%1]").arg(result.error.serverCode());
            }
            setStatus(QStringLiteral("Failed: %1").arg(detail), true);
        } else {
            setStatus(QStringLiteral("Connected. %1 bucket(s) visible. Region \"%2\".")
                          .arg(result.value.size())
                          .arg(probe->effectiveRegion()),
                      false);
        }
        probe->deleteLater();
    });
}

void ConnectDialog::onAccept() {
    const S3Config cfg = readForm();
    const QString problem = cfg.validate();
    if (!problem.isEmpty()) {
        setStatus(problem, true);
        return;
    }

    // Connecting with unsaved edits is common enough that refusing would be
    // annoying; save silently instead, and report it.
    if (m_dirty && !cfg.name.isEmpty()) {
        QString error;
        if (!commitForm(&error)) {
            setStatus(error.isEmpty() ? QStringLiteral("Could not save the connection.")
                                      : QStringLiteral("Could not save settings: %1").arg(error),
                      true);
            return;
        }
    }

    m_current = cfg;
    accept();
}

void ConnectDialog::preselect(const QString &name) {
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == name) {
            m_list->setCurrentRow(i);
            return;
        }
    }
}

} // namespace s3desktop
