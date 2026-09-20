#include "core/ConnectionStore.h"

#include "core/CredentialStore.h"
#include "core/Log.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace s3desktop {

namespace {

/// Read a value from an environment variable, treating empty as unset.
QString env(const char *name) {
    const QString v = QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(name));
    return v.trimmed();
}

/// Truthy words accepted for USE_SSL, matching what the original accepted.
bool envBool(const QString &v) {
    const QString s = v.trimmed().toLower();
    return s == QStringLiteral("1") || s == QStringLiteral("true") || s == QStringLiteral("yes") ||
           s == QStringLiteral("on");
}

} // namespace

ConnectionStore::ConnectionStore() : m_credentials(std::make_unique<CredentialStore>()) {}
ConnectionStore::~ConnectionStore() = default;

bool ConnectionStore::load(QString *warning) {
    m_configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (m_configDir.isEmpty()) {
        m_configDir = QDir::homePath() + QStringLiteral("/.s3desktop");
    }
    if (!QDir().mkpath(m_configDir)) {
        if (warning) {
            *warning = QStringLiteral("Could not create the configuration directory %1")
                           .arg(m_configDir);
        }
        return false;
    }

    if (!m_credentials->initialise(m_configDir) && warning) {
        *warning = QStringLiteral("Secrets will not be stored securely: %1")
                       .arg(m_credentials->lastError());
    }

    m_connections.clear();

    QFile f(QDir(m_configDir).filePath(QStringLiteral("settings.json")));
    if (!f.exists()) {
        return true;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        if (warning) {
            *warning = QStringLiteral("Could not read settings.json: %1").arg(f.errorString());
        }
        return true;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    f.close();

    if (parseError.error != QJsonParseError::NoError) {
        // Do not overwrite a file we could not understand: the user may be able
        // to recover it, and silently replacing it would destroy that chance.
        if (warning) {
            *warning = QStringLiteral("settings.json is not valid JSON (%1). It was left "
                                      "untouched and no connections were loaded.")
                           .arg(parseError.errorString());
        }
        return true;
    }

    bool migratedPlaintext = false;

    const QJsonArray arr = doc.object().value(QStringLiteral("connections")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();

        S3Config c;
        c.name = o.value(QStringLiteral("name")).toString();
        c.endpoint = o.value(QStringLiteral("endpoint")).toString();
        c.accessKey = o.value(QStringLiteral("accessKey")).toString();
        c.secretKey = o.value(QStringLiteral("secretKey")).toString();
        c.bucket = o.value(QStringLiteral("bucket")).toString();
        c.prefix = o.value(QStringLiteral("prefix")).toString();
        c.region = o.value(QStringLiteral("region")).toString();
        c.useSsl = o.value(QStringLiteral("usessl")).toBool();

        // Fields this build adds; absent in files written by the original app.
        c.targetId = o.value(QStringLiteral("target")).toString();
        c.addressingStyle = o.value(QStringLiteral("addressing")).toInt(0);
        c.tls = static_cast<TlsPolicy>(o.value(QStringLiteral("tls")).toInt(
            c.useSsl ? static_cast<int>(TlsPolicy::VerifyStrict)
                     : static_cast<int>(TlsPolicy::Disabled)));

        if (c.name.isEmpty() || c.name == S3Config::Transient) {
            continue;
        }

        // Same migration the original performed against the OS keychain: a
        // plaintext secret is moved into the credential store and reported so
        // the file can be rewritten without it.
        if (!c.secretKey.isEmpty() && m_credentials->isOsProtected()) {
            if (m_credentials->set(c.name, c.secretKey)) {
                migratedPlaintext = true;
            }
        } else if (c.secretKey.isEmpty()) {
            c.secretKey = m_credentials->get(c.name);
            if (c.secretKey.isEmpty()) {
                // Not fatal here — the user may simply not have saved one yet —
                // but it is the answer to "why can't I connect" often enough to
                // be worth a line naming the connection.
                Log::write(Log::core(), 1,
                           QStringLiteral("connection \"%1\" has no usable secret key; its "
                                          "requests will fail with a signature error")
                               .arg(c.name));
            }
        }

        m_connections.append(c);
    }

    if (migratedPlaintext) {
        // Best effort: a failure here leaves the plaintext in place rather than
        // losing the secret.
        QString ignored;
        save(&ignored);
    }

    return true;
}

bool ConnectionStore::save(QString *error) {
    const QString path = QDir(m_configDir).filePath(QStringLiteral("settings.json"));

    QJsonArray arr;
    for (const S3Config &c : m_connections) {
        if (c.name == S3Config::Transient) {
            continue;
        }

        QJsonObject o;
        o.insert(QStringLiteral("name"), c.name);
        o.insert(QStringLiteral("endpoint"), c.endpoint);
        o.insert(QStringLiteral("accessKey"), c.accessKey);
        o.insert(QStringLiteral("bucket"), c.bucket);
        o.insert(QStringLiteral("prefix"), c.prefix);
        o.insert(QStringLiteral("region"), c.region);
        o.insert(QStringLiteral("usessl"), c.useSsl);
        o.insert(QStringLiteral("target"), c.targetId);
        o.insert(QStringLiteral("addressing"), c.addressingStyle);
        o.insert(QStringLiteral("tls"), static_cast<int>(c.tls));

        // Blank the secret when the credential store took it. If storage fails,
        // fall back to writing it in the file so the connection keeps working —
        // the same trade-off the original made.
        QString secret = c.secretKey;
        if (!secret.isEmpty() && m_credentials->isOsProtected()) {
            if (m_credentials->set(c.name, secret)) {
                secret.clear();
            }
        }
        o.insert(QStringLiteral("secretKey"), secret);

        arr.append(o);
    }

    QJsonObject root;
    root.insert(QStringLiteral("connections"), arr);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = f.errorString();
        }
        return false;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

void ConnectionStore::upsert(const S3Config &cfg) {
    if (cfg.name == S3Config::Transient) {
        return;
    }
    for (S3Config &existing : m_connections) {
        if (existing.name == cfg.name) {
            existing = cfg;
            return;
        }
    }
    m_connections.append(cfg);
}

bool ConnectionStore::removeByName(const QString &name) {
    for (int i = 0; i < m_connections.size(); ++i) {
        if (m_connections[i].name == name) {
            m_connections.removeAt(i);
            if (name != S3Config::Transient) {
                m_credentials->remove(name);
            }
            return true;
        }
    }
    return false;
}

int ConnectionStore::indexOf(const QString &name) const {
    for (int i = 0; i < m_connections.size(); ++i) {
        if (m_connections[i].name == name) {
            return i;
        }
    }
    return -1;
}

QString ConnectionStore::uniqueName(const QString &base) const {
    if (indexOf(base) < 0) {
        return base;
    }
    for (int n = 2; n < 1000; ++n) {
        const QString candidate = QStringLiteral("%1 (%2)").arg(base).arg(n);
        if (indexOf(candidate) < 0) {
            return candidate;
        }
    }
    return base;
}

S3Config ConnectionStore::fromCommandLine(QStringList *warnings) {
    // The original accepted --endpoint/ENDPOINT and friends. The environment is
    // read rather than registerOption'd so that this function stays callable
    // before QApplication owns the argument list.
    S3Config c;

    c.endpoint = env("ENDPOINT");
    c.accessKey = env("ACCESS_KEY");
    c.secretKey = env("SECRET_KEY");
    c.bucket = env("BUCKET");
    c.prefix = env("PREFIX");
    c.region = env("REGION");
    c.useSsl = envBool(env("USE_SSL"));

    if (const QString t = env("TARGET"); !t.isEmpty()) {
        c.targetId = t;
    }

    if (c.endpoint.isEmpty() || c.accessKey.isEmpty()) {
        return {};
    }

    c.name = S3Config::Transient;
    if (warnings && c.secretKey.isEmpty()) {
        warnings->append(QStringLiteral("A transient connection was created from the "
                                        "environment without a secret key."));
    }
    return c;
}

} // namespace s3desktop
