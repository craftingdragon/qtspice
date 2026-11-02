#include <QtSpice/QSpiceSession.h>
#include <QtSpice/QSpiceWidget.h>
#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QUrl>
#include <QFile>
#include <QDebug>
#include <QTimer>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("simple-viewer"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("QtSpice direct SPICE viewer"));
    parser.addHelpOption();
    QCommandLineOption snapshot(QStringLiteral("snapshot"), QStringLiteral("Save the first received frame and exit"), QStringLiteral("path"));
    parser.addOption(snapshot);
    parser.addPositionalArgument(QStringLiteral("target"), QStringLiteral("spice://host:port or connection.vv"));
    parser.process(app);
    if (parser.positionalArguments().size() != 1) parser.showHelp(1);
    const QString target = parser.positionalArguments().first();
    QtSpice::ConnectionConfig config;
    if (target.endsWith(QStringLiteral(".vv"), Qt::CaseInsensitive)) {
        QFile file(target);
        if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot open connection file");
        QString error;
        auto parsed = QtSpice::parseVv(file.readAll(), &error);
        if (!parsed) qFatal("Invalid connection file: %s", qPrintable(error));
        config = std::move(*parsed);
    } else {
        QUrl url(target);
        if (url.scheme() != QStringLiteral("spice") || url.host().isEmpty() || url.port() < 1 || url.port() > 65535) parser.showHelp(1);
        config.host = url.host();
        config.port = quint16(url.port());
    }
    QtSpice::Session session;
    QtSpice::Widget widget;
    widget.setSession(&session);
    if (parser.isSet(snapshot)) {
        auto latest = std::make_shared<QImage>();
        QObject::connect(&session, &QtSpice::Session::frameReady, &app, [latest](const QImage &image) {
            if (!image.isNull()) *latest = image;
        });
        QTimer::singleShot(2000, &app, [&app, &parser, snapshot, latest] {
            if (latest->isNull() || !latest->save(parser.value(snapshot))) {
                qWarning() << "Could not save snapshot";
                app.exit(2);
            } else {
                app.quit();
            }
        });
    }
    QObject::connect(&session, &QtSpice::Session::errorOccurred, &widget, [&widget](const QString &message) {
        qWarning().noquote() << "SPICE connection:" << message;
        QMessageBox::warning(&widget, QStringLiteral("SPICE connection"), message);
    });
    widget.resize(1024, 768);
    widget.show();
    session.connectToConfig(config);
    return app.exec();
}
