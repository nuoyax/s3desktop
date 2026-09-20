#pragma once

#include "core/S3Config.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace us3 {

class ConnectionStore;
class S3Client;

/// The connection manager: pick a saved connection, or edit one.
///
/// The original was a single form with a list beside it and a separate
/// free-floating bucket window. This version keeps the same capabilities but
/// adds two things the original could not do without a round trip through a
/// failure dialog: choosing the provider profile, and testing the connection
/// before saving it.
class ConnectDialog : public QDialog {
    Q_OBJECT

public:
    ConnectDialog(ConnectionStore *store, S3Client *client, QWidget *parent = nullptr);

    /// The connection the user chose (Connect) or the last one selected
    /// (Save). Meaningful only after the dialog is accepted.
    S3Config selectedConfig() const { return m_current; }

    /// Pre-select the connection named `name`, if it exists.
    void preselect(const QString &name);

signals:
    /// Emitted when the user asked to manage buckets for the given config.
    /// MainWindow owns the bucket window so it is not modal to this dialog.
    void manageBucketsRequested(const S3Config &config);

private slots:
    void onSelectionChanged();
    void onAdd();
    void onCopy();
    void onDelete();
    void onSave();
    void onTest();
    void onAccept();
    void onFormEdited();

private:
    void buildUi();
    QWidget *buildListPane();
    QWidget *buildFormPane();
    void refreshList();
    void loadIntoForm(const S3Config &cfg);
    S3Config readForm() const;
    void setStatus(const QString &text, bool isError);
    void updateButtons();
    bool commitForm(QString *error);

    ConnectionStore *m_store = nullptr;
    S3Client *m_client = nullptr;

    QListWidget *m_list = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_copyButton = nullptr;

    QLineEdit *m_name = nullptr;
    QComboBox *m_target = nullptr;
    QLineEdit *m_endpoint = nullptr;
    QLabel *m_endpointHint = nullptr;
    QLineEdit *m_accessKey = nullptr;
    QLineEdit *m_secretKey = nullptr;
    QLineEdit *m_bucket = nullptr;
    QLineEdit *m_region = nullptr;
    QLabel *m_regionHint = nullptr;
    QLineEdit *m_prefix = nullptr;
    QComboBox *m_tls = nullptr;
    QComboBox *m_addressing = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_testButton = nullptr;

    S3Config m_current;
    bool m_loadingForm = false;
    bool m_dirty = false;
};

} // namespace us3
