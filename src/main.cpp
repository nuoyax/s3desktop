#include "core/Log.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QStyleFactory>
#include <QTimer>

/// Command-line surface.
///
/// The original app was configured entirely through environment variables, which
/// makes it awkward to run two connections side by side or to discover what is
/// available. The same names still work (see ConnectionStore::fromCommandLine),
/// and the flags below are added so a connection can be stated outright. When
/// flags are present they win over the environment.
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("us3qt"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("us3qt"));
    QApplication::setOrganizationDomain(QStringLiteral("us3qt.local"));

    // Logging goes up before anything else can fail, and before the parser, so
    // that even a bad command line is recorded.
    const QString logPath = us3::Log::install();
    us3::Log::write(us3::Log::app(), 0,
                    QStringLiteral("us3qt %1 starting  pid=%2  log=%3")
                        .arg(QApplication::applicationVersion())
                        .arg(QCoreApplication::applicationPid())
                        .arg(logPath.isEmpty() ? QStringLiteral("(disabled)") : logPath));

    // A stable palette regardless of the system theme: the stylesheet assumes
    // light surfaces, and letting Windows switch to dark mid-session would leave
    // text unreadable.
    if (const QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        QApplication::setStyle(const_cast<QStyle *>(fusion));
    }

    us3::Theme::apply(app);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("A Qt front end for S3-compatible object storage."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption endpointOption({QStringLiteral("e"), QStringLiteral("endpoint")},
                                            QStringLiteral("Endpoint host, e.g. "
                                                           "s3-cn-bj.ufileos.com"),
                                            QStringLiteral("host"));
    const QCommandLineOption accessKeyOption({QStringLiteral("a"), QStringLiteral("access-key")},
                                             QStringLiteral("Access key"), QStringLiteral("key"));
    const QCommandLineOption secretKeyOption({QStringLiteral("s"), QStringLiteral("secret-key")},
                                             QStringLiteral("Secret key"), QStringLiteral("key"));
    const QCommandLineOption bucketOption({QStringLiteral("b"), QStringLiteral("bucket")},
                                          QStringLiteral("Bucket to open"),
                                          QStringLiteral("name"));
    const QCommandLineOption prefixOption(QStringLiteral("prefix"),
                                          QStringLiteral("Key prefix to browse from"),
                                          QStringLiteral("prefix"));
    const QCommandLineOption regionOption(QStringLiteral("region"),
                                          QStringLiteral("Region; empty derives it from the "
                                                         "endpoint"),
                                          QStringLiteral("region"));
    const QCommandLineOption targetOption(QStringLiteral("target"),
                                          QStringLiteral("Provider profile: generic, aws-s3, "
                                                         "ucloud-us3 or minio"),
                                          QStringLiteral("id"));
    const QCommandLineOption sslOption(QStringLiteral("ssl"),
                                       QStringLiteral("Use https (default)"));
    const QCommandLineOption noSslOption(QStringLiteral("no-ssl"),
                                         QStringLiteral("Use plain http"));

    parser.addOption(endpointOption);
    parser.addOption(accessKeyOption);
    parser.addOption(secretKeyOption);
    parser.addOption(bucketOption);
    parser.addOption(prefixOption);
    parser.addOption(regionOption);
    parser.addOption(targetOption);
    parser.addOption(sslOption);
    parser.addOption(noSslOption);

    parser.process(app);

    // Flags override the environment, so a shell that exports ENDPOINT can still
    // be pointed somewhere else for one run.
    if (parser.isSet(endpointOption)) {
        qputenv("ENDPOINT", parser.value(endpointOption).toUtf8());
        qputenv("ACCESS_KEY", parser.value(accessKeyOption).toUtf8());
        qputenv("SECRET_KEY", parser.value(secretKeyOption).toUtf8());
        if (parser.isSet(bucketOption)) {
            qputenv("BUCKET", parser.value(bucketOption).toUtf8());
        }
        if (parser.isSet(prefixOption)) {
            qputenv("PREFIX", parser.value(prefixOption).toUtf8());
        }
        if (parser.isSet(regionOption)) {
            qputenv("REGION", parser.value(regionOption).toUtf8());
        }
        if (parser.isSet(targetOption)) {
            qputenv("TARGET", parser.value(targetOption).toUtf8());
        }
        if (parser.isSet(sslOption)) {
            qputenv("USE_SSL", "1");
        }
        if (parser.isSet(noSslOption)) {
            qputenv("USE_SSL", "0");
        }
    }

    us3::MainWindow window;
    window.show();

    // Sized after show() so the initial layout is measured against the real
    // window, which keeps the details pane and ribbon from being clipped on the
    // first frame.
    QTimer::singleShot(0, &window, [&window]() { window.resize(1180, 720); });

    return app.exec();
}
