#include "core/CredentialStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <wincrypt.h>
#endif

namespace us3 {

namespace {

/// Turn an arbitrary connection name into something safe to use as a filename.
/// Hashing keeps names that differ only in characters the filesystem folds
/// together (case, trailing dots) from colliding.
QString accountToFileName(const QString &account) {
    const QByteArray digest =
        QCryptographicHash::hash(account.toUtf8(), QCryptographicHash::Sha256).toHex().left(32);
    return QString::fromLatin1(digest) + QStringLiteral(".bin");
}

#ifdef Q_OS_WIN
bool dpapiProtect(const QByteArray &plain, QByteArray *out, QString *error) {
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()));
    in.cbData = static_cast<DWORD>(plain.size());

    DATA_BLOB result{};
    if (!CryptProtectData(&in, L"us3qt", nullptr, nullptr, nullptr, 0, &result)) {
        if (error) {
            *error = QStringLiteral("CryptProtectData failed with error %1").arg(GetLastError());
        }
        return false;
    }
    *out = QByteArray(reinterpret_cast<const char *>(result.pbData),
                      static_cast<qsizetype>(result.cbData));
    LocalFree(result.pbData);
    return true;
}

bool dpapiUnprotect(const QByteArray &cipher, QByteArray *out, QString *error) {
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(cipher.constData()));
    in.cbData = static_cast<DWORD>(cipher.size());

    DATA_BLOB result{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &result)) {
        if (error) {
            *error = QStringLiteral("CryptUnprotectData failed with error %1").arg(GetLastError());
        }
        return false;
    }
    *out = QByteArray(reinterpret_cast<const char *>(result.pbData),
                      static_cast<qsizetype>(result.cbData));
    LocalFree(result.pbData);
    return true;
}
#endif

} // namespace

CredentialStore::CredentialStore() = default;
CredentialStore::~CredentialStore() = default;

bool CredentialStore::initialise(const QString &configDir) {
    m_dir = QDir(configDir).filePath(QStringLiteral("credentials"));
    QDir dir;
    if (!dir.mkpath(m_dir)) {
        m_lastError = QStringLiteral("Could not create %1").arg(m_dir);
        return false;
    }
    return true;
}

QString CredentialStore::blobPath(const QString &account) const {
    return QDir(m_dir).filePath(accountToFileName(account));
}

bool CredentialStore::set(const QString &account, const QString &secret) {
    if (m_dir.isEmpty()) {
        m_lastError = QStringLiteral("Credential store was not initialised");
        return false;
    }

    QByteArray payload = secret.toUtf8();

#ifdef Q_OS_WIN
    QByteArray sealed;
    if (!dpapiProtect(payload, &sealed, &m_lastError)) {
        return false;
    }
    payload = sealed;
#endif

    QFile f(blobPath(account));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_lastError = f.errorString();
        return false;
    }
    // Tighten before writing so the secret is never briefly world-readable.
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (f.write(payload) != payload.size()) {
        m_lastError = f.errorString();
        return false;
    }
    f.close();
    return true;
}

QString CredentialStore::get(const QString &account) const {
    if (m_dir.isEmpty()) {
        return {};
    }

    QFile f(blobPath(account));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray payload = f.readAll();
    f.close();

    if (payload.isEmpty()) {
        return {};
    }

#ifdef Q_OS_WIN
    QByteArray plain;
    if (!dpapiUnprotect(payload, &plain, &m_lastError)) {
        // Wrong user account, or a file written by an older build: report
        // nothing rather than a garbled secret.
        return {};
    }
    payload = plain;
#endif

    return QString::fromUtf8(payload);
}

bool CredentialStore::remove(const QString &account) {
    if (m_dir.isEmpty()) {
        return false;
    }
    QFile f(blobPath(account));
    if (!f.exists()) {
        return true;
    }
    return f.remove();
}

QString CredentialStore::backendName() const {
#ifdef Q_OS_WIN
    return QStringLiteral("Windows DPAPI");
#else
    return QStringLiteral("owner-only file");
#endif
}

bool CredentialStore::isOsProtected() const {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

} // namespace us3
