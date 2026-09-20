#pragma once

#include <QString>

namespace s3desktop {

/// Storage for secret keys, kept out of the settings file.
///
/// The original app used the OS keychain via go-keyring, falling back to
/// plaintext in settings.json when no backend was available. This build does the
/// same job with the OS facility that needs no extra dependency on Windows:
/// DPAPI (`CryptProtectData`), which ties the ciphertext to the current user
/// account. On platforms without an equivalent built in, the implementation
/// degrades to a file readable only by the current user, and says so through
/// backendName() so the UI can warn.
///
/// The interface is deliberately narrow so swapping in a real cross-platform
/// keychain later touches only this file's implementation.
class CredentialStore {
public:
    CredentialStore();
    ~CredentialStore();

    CredentialStore(const CredentialStore &) = delete;
    CredentialStore &operator=(const CredentialStore &) = delete;

    /// Direct the store at a directory. Created if missing.
    /// Returns false if the directory cannot be prepared.
    bool initialise(const QString &configDir);

    /// Store or replace the secret for `account`.
    bool set(const QString &account, const QString &secret);

    /// Retrieve the secret for `account`. Returns an empty string when absent
    /// or undecryptable — callers treat that as "no stored secret".
    QString get(const QString &account) const;

    /// Remove the secret for `account`. Succeeds when nothing was stored.
    bool remove(const QString &account);

    /// Human-readable name of the backend in use, for display in the UI.
    QString backendName() const;

    /// Whether secrets are protected by the OS or only by file permissions.
    bool isOsProtected() const;

    /// Last error from set/get/remove, for logging.
    QString lastError() const { return m_lastError; }

private:
    QString blobPath(const QString &account) const;

    QString m_dir;
    mutable QString m_lastError;
};

} // namespace s3desktop
