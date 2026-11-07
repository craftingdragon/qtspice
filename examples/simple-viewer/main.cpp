#include "../monitorwindows.h"
#include "../reconnect.h"
#include <QFileDialog>
#include "../audiomenu.h"
#include "../spicelogging.h"
#include "../sharedfolder.h"
#include <QtSpice/QSpiceSession.h>
#include <QtSpice/QSpiceWidget.h>
#include "../viewersettings.h"
#include "../scalingmenu.h"
#include "../inputmenu.h"
#include "../fullscreen.h"
#include <QApplication>
#include <QLoggingCategory>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QUrl>
#include <QFile>
#include <QDebug>
#include <QTimer>
#include <QMainWindow>
#include <QMenuBar>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    SpiceLogging::configure();
    QCoreApplication::setApplicationName(QStringLiteral("simple-viewer"));
    QCoreApplication::setOrganizationName(QStringLiteral("QtSpice"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("QtSpice direct SPICE viewer"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("performance-stats"), QStringLiteral("Enable one-second performance diagnostics")});
    SharedFolder::addOptions(parser);
    QCommandLineOption snapshot(QStringLiteral("snapshot"), QStringLiteral("Save the first received frame and exit"), QStringLiteral("path"));
    parser.addOption(snapshot);
    QCommandLineOption proxy(QStringLiteral("proxy"), QStringLiteral("Network proxy: http://host:port or socks5://host:port"), QStringLiteral("url"));
    parser.addOption(proxy);
    parser.addPositionalArgument(QStringLiteral("target"), QStringLiteral("spice://host:port or connection.vv"));
    parser.process(app);
    if (parser.isSet(QStringLiteral("performance-stats")))
        QLoggingCategory::setFilterRules(QStringLiteral("qtspice.performance.debug=true"));
    std::optional<QtSpice::SharedDirectory> sharedDirectory;
    QString sharingError;
    if (!SharedFolder::parse(parser, sharedDirectory, sharingError)) { qCritical().noquote() << sharingError; return 1; }
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
    if (parser.isSet(proxy)) {
        QString error;
        const auto parsed = QtSpice::parseProxyUrl(parser.value(proxy), &error);
        if (!parsed) { qCritical().noquote() << error; return 1; }
        config.networkProxy = parsed->toString(QUrl::FullyEncoded);
    }
    QtSpice::Session session;
    QMainWindow window;
    auto *widget = new QtSpice::Widget(&window);
    widget->setSession(&session);
    window.setCentralWidget(widget);
    AudioMenu::add(window.menuBar(), &session);
    SharedFolder::addMenu(window.menuBar(), &session);
    auto *viewMenu = window.menuBar()->addMenu(QStringLiteral("View"));
    addScalingMenu(viewMenu, widget);
    addInputMenu(window.menuBar(), window.statusBar(), widget, &session);
    new Fullscreen(&window, viewMenu, widget);
    new MonitorWindows(&window, &session, viewMenu);
    auto *resizeAction = viewMenu->addAction(QStringLiteral("Auto Resize Guest"));
    resizeAction->setCheckable(true);
    resizeAction->setChecked(ViewerSettings::autoResizeGuest());
    widget->setAutoResizeGuest(resizeAction->isChecked());
    QObject::connect(resizeAction, &QAction::toggled, widget, [widget](bool enabled) {
        widget->setAutoResizeGuest(enabled);
        ViewerSettings::setAutoResizeGuest(enabled);
    });
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
    if (!parser.isSet(snapshot)) {
        auto *reconnect = new Reconnect(&window, &session);
        reconnect->sharing_ = sharedDirectory;
        auto filePath = std::make_shared<QString>(target.endsWith(QStringLiteral(".vv"), Qt::CaseInsensitive) ? target : QString());
        reconnect->source = [filePath, config](Reconnect::Result done) mutable {
            auto next = config;
            if (!filePath->isEmpty()) {
                QFile file(*filePath); QString error;
                if (!file.open(QIODevice::ReadOnly)) { done({}, QStringLiteral("Cannot read connection file")); return; }
                auto parsed = QtSpice::parseVv(file.read(1024 * 1024 + 1), &error);
                if (!parsed) { done({}, error); return; }
                next = *parsed; next.networkProxy = config.networkProxy;
            }
            done(next, {});
        };
        QMenu *connectionMenu = nullptr;
        for (auto *action : window.menuBar()->actions()) if (action->text() == QStringLiteral("Connection")) connectionMenu = action->menu();
        auto *load = connectionMenu->addAction(QStringLiteral("Load Connection File…"));
        QObject::connect(&session, &QtSpice::Session::stateChanged, load, [load](auto state) { load->setEnabled(state == QtSpice::Session::State::Disconnected); });
        QObject::connect(load, &QAction::triggered, &window, [filePath, reconnect, &window] {
            const auto path = QFileDialog::getOpenFileName(&window, QStringLiteral("Load SPICE connection"), {}, QStringLiteral("SPICE connection (*.vv)"));
            if (!path.isEmpty()) { *filePath = path; reconnect->start(); }
        });
    }
    window.resize(1024, 768);
    window.show();
    config.sharedDirectory = sharedDirectory;
    session.connectToConfig(config);
    return app.exec();
}
