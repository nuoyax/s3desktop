#pragma once

#include "core/S3Config.h"
#include "core/S3Types.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace us3 {

class S3Client;

/// Bucket administration: list, create, delete, and pick.
///
/// The original opened this in a second window that shared no state with the
/// main one, so choosing a bucket there did not reach the connection form. This
/// is a dialog with an explicit arrow back to the caller: "Use selected" hands
/// the name to whoever opened it.
class BucketManager : public QDialog {
    Q_OBJECT

public:
    explicit BucketManager(QWidget *parent = nullptr);

    /// Configure which connection is being administered and reload.
    void setConfig(const S3Config &config);

    /// The bucket chosen via "Use selected", if any.
    QString chosenBucket() const { return m_chosen; }

    /// Called by the parent once it has taken the selection, so a second
    /// request is not silently ignored on reuse.
    void clearChoice() { m_chosen.clear(); }

private slots:
    void onRefresh();
    void onCreate();
    void onDelete();
    void onUseSelected();

private:
    void buildUi();
    void setBusy(bool busy);
    void setStatus(const QString &text, bool isError);
    void refreshTable(const QList<BucketInfo> &buckets);

    S3Client *m_client = nullptr;
    S3Config m_config;

    QTableWidget *m_table = nullptr;
    QLineEdit *m_newBucket = nullptr;
    QPushButton *m_createButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_useButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QLabel *m_status = nullptr;
    QString m_chosen;
};

} // namespace us3
