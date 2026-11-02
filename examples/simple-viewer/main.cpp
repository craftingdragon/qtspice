#include <QtSpice/QSpiceSession.h>
#include <QtSpice/QSpiceWidget.h>
#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QUrl>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("simple-viewer"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("QtSpice direct SPICE viewer"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("url"), QStringLiteral("spice://host:port"));
    parser.process(app);
    if (parser.positionalArguments().size() != 1) parser.showHelp(1);
    QUrl url(parser.positionalArguments().first());
    if (url.scheme() != QStringLiteral("spice") || url.host().isEmpty() || url.port() < 1 || url.port() > 65535) parser.showHelp(1);
    QtSpice::Session session;
    QtSpice::Widget widget;
    widget.setSession(&session);
    QObject::connect(&session, &QtSpice::Session::errorOccurred, &widget, [&widget](const QString &message) { QMessageBox::warning(&widget, QStringLiteral("SPICE connection"), message); });
    widget.resize(1024, 768);
    widget.show();
    session.connectToHost(url.host(), quint16(url.port()));
    return app.exec();
}
