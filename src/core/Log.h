#pragma once

#include <QLoggingCategory>
#include <QString>

namespace us3 {

/// Application logging: a rotating file beside settings.json, plus Qt's own
/// messages.
///
/// The original app logged nothing at all — a failed connection produced one
/// sentence in the status bar, and the request that caused it left no trace. For
/// a tool whose whole job is talking to providers that disagree about signing,
/// addressing and error formats, that means every bug report starts with "it
/// doesn't work".
///
/// Everything a request does is written here: the URL actually built, the host
/// actually signed, the status, and the response body when it is an error.
/// Secrets are never written; see redact().
namespace Log {

/// Categories, so a --verbose could be selective later.
Q_DECLARE_LOGGING_CATEGORY(net)
Q_DECLARE_LOGGING_CATEGORY(core)
Q_DECLARE_LOGGING_CATEGORY(ui)
Q_DECLARE_LOGGING_CATEGORY(app)

/// Direct Qt's messages into the log file and open it.
///
/// `path` empty means the default location, or BUCKETEXPLORER_LOG if set. Setting
/// BUCKETEXPLORER_LOG=off disables logging entirely, which matters when the log file is
/// itself suspected of causing trouble. Safe to call once, early in main();
/// calling it twice is a no-op. Returns the file actually opened, or empty.
QString install(const QString &path = {});

/// The file currently being written, or empty when logging is off.
QString filePath();

/// Whether logging is on. Call sites check this before building an expensive
/// message: a 50k-object listing should not format strings for a disabled log.
bool enabled();

/// Mask a credential so it can appear in the log without being usable.
/// Keeps the first four and last two characters, e.g. "GK57…6ef".
QString redact(const QString &secret);

/// Write one line. `level`: 0 info, 1 warn, 2 error, 3 debug.
void write(const QLoggingCategory &category, int level, const QString &message);

/// Truncate a response body for logging: collapse whitespace, cap the length.
QString summariseBody(const QByteArray &body, int limit = 600);

} // namespace Log
} // namespace us3
