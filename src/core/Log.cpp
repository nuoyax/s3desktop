#include "core/Log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

namespace us3 {

Q_LOGGING_CATEGORY(Log::net, "bucketexplorer.net")
Q_LOGGING_CATEGORY(Log::core, "bucketexplorer.core")
Q_LOGGING_CATEGORY(Log::ui, "bucketexplorer.ui")
Q_LOGGING_CATEGORY(Log::app, "bucketexplorer.app")

namespace {

QMutex g_mutex;
QFile *g_file = nullptr;
QString g_path;
bool g_enabled = false;
bool g_installed = false;

/// "off"/"none"/"0" disables logging, which matters when the file itself is
/// suspected of causing trouble.
bool loggingDisabledByEnv() {
    const QString v =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("BUCKETEXPLORER_LOG")).trimmed().toLower();
    return v == QStringLiteral("off") || v == QStringLiteral("none") || v == QStringLiteral("0");
}

QString explicitPathFromEnv() {
    return QProcessEnvironment::systemEnvironment().value(QStringLiteral("BUCKETEXPLORER_LOG")).trimmed();
}

/// A timestamp with milliseconds: upload and listing failures are often a
/// sequence of events that matter in order.
QString stamp() {
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
}

const char *levelName(int level) {
    switch (level) {
    case 0:
        return "INFO ";
    case 1:
        return "WARN ";
    case 2:
        return "ERROR";
    default:
        return "DEBUG";
    }
}

/// Qt's own messages (TLS warnings, plugin errors, QNetworkReply internals) go
/// to the same file rather than to a console a GUI build does not have.
void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    int level = 0;
    switch (type) {
    case QtDebugMsg:
        level = 3;
        break;
    case QtInfoMsg:
        level = 0;
        break;
    case QtWarningMsg:
        level = 1;
        break;
    case QtCriticalMsg:
    case QtFatalMsg:
        level = 2;
        break;
    }

    QMutexLocker lock(&g_mutex);
    const QString line = QStringLiteral("%1 %2 qt      : %3")
                             .arg(stamp(), QString::fromLatin1(levelName(level)), msg);
    if (g_file) {
        QTextStream stream(g_file);
        stream << line << Qt::endl;
        stream.flush();
    }
    // Also to stderr: when the app is started from a terminal, the Qt messages
    // show up live, which is the whole point of installing a handler.
    fprintf(stderr, "%s\n", line.toUtf8().constData());
    fflush(stderr);
}

} // namespace

QString Log::install(const QString &path) {
    QMutexLocker lock(&g_mutex);
    if (g_installed) {
        return g_path;
    }
    g_installed = true;

    if (loggingDisabledByEnv()) {
        g_enabled = false;
        return {};
    }

    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty()) {
        dir = QDir::homePath() + QStringLiteral("/.bucketexplorer");
    }
    QDir().mkpath(dir);

    g_path = path;
    if (g_path.isEmpty()) {
        const QString fromEnv = explicitPathFromEnv();
        g_path = !fromEnv.isEmpty() ? fromEnv
                                    : QDir(dir).filePath(QStringLiteral("bucketexplorer.log"));
    }

    // One prior log is kept: enough to diagnose an intermittent failure across a
    // restart, not enough to accumulate indefinitely.
    if (QFileInfo::exists(g_path)) {
        const QString previous = g_path + QStringLiteral(".1");
        QFile::remove(previous);
        QFile::rename(g_path, previous);
    }

    auto *file = new QFile(g_path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        delete file;
        g_enabled = false;
        return {};
    }
    g_file = file;
    g_enabled = true;

    qInstallMessageHandler(messageHandler);
    return g_path;
}

QString Log::filePath() {
    QMutexLocker lock(&g_mutex);
    return g_enabled ? g_path : QString();
}

bool Log::enabled() {
    QMutexLocker lock(&g_mutex);
    return g_enabled;
}

QString Log::redact(const QString &secret) {
    if (secret.isEmpty()) {
        return QStringLiteral("(empty)");
    }
    if (secret.size() <= 6) {
        return QStringLiteral("****");
    }
    return secret.left(4) + QStringLiteral("…") + secret.right(2);
}

void Log::write(const QLoggingCategory &category, int level, const QString &message) {
    QMutexLocker lock(&g_mutex);
    if (!g_file) {
        return;
    }
    const QString line = QStringLiteral("%1 %2 %3: %4")
                             .arg(stamp(), QString::fromLatin1(levelName(level)),
                                  QString::fromLatin1(category.categoryName()), message);
    QTextStream stream(g_file);
    stream << line << Qt::endl;
    stream.flush();
}

QString Log::summariseBody(const QByteArray &body, int limit) {
    if (body.isEmpty()) {
        return QStringLiteral("(empty body)");
    }
    QString text = QString::fromUtf8(body);
    text.replace(QRegularExpression(QStringLiteral("[\\r\\n\\t]+")), QStringLiteral(" "));
    text = text.simplified();
    if (text.size() > limit) {
        text = text.left(limit) + QStringLiteral("… (truncated)");
    }
    return text;
}

} // namespace us3
