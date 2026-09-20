#pragma once

#include "core/S3Config.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

namespace us3 {

class CredentialStore;

/// The saved-connection list, backed by settings.json plus the credential store.
///
/// Reads the same file layout as the original app so an existing installation
/// carries over, including its plaintext secrets: those are migrated into the
/// credential store on load and blanked in the file, exactly as the original did
/// with the OS keychain.
class ConnectionStore {
public:
    ConnectionStore();
    ~ConnectionStore();

    /// Load settings from the platform config directory.
    /// Returns false only when the directory could not be prepared; a missing or
    /// corrupt settings file yields an empty list and an error message rather
    /// than a failure to start.
    bool load(QString *warning = nullptr);

    /// Write the list back, with secrets moved into the credential store.
    bool save(QString *error = nullptr);

    /// The platform config directory this store uses.
    QString configDir() const { return m_configDir; }

    QList<S3Config> connections() const { return m_connections; }

    /// Insert or replace by name. A transient connection is never stored.
    void upsert(const S3Config &cfg);

    /// Remove by name. Also forgets the stored secret.
    bool removeByName(const QString &name);

    /// Index of `name`, or -1.
    int indexOf(const QString &name) const;

    /// Unique name derived from `base`, e.g. "prod" -> "prod (2)".
    QString uniqueName(const QString &base) const;

    /// Build a transient connection from command-line flags and environment
    /// variables. Returns an empty optional-like result when nothing was given.
    static S3Config fromCommandLine(QStringList *warnings = nullptr);

    CredentialStore *credentials() { return m_credentials.get(); }

private:
    QString m_configDir;
    QList<S3Config> m_connections;
    std::unique_ptr<CredentialStore> m_credentials;
};

} // namespace us3
