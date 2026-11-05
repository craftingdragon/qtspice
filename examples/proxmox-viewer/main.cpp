#include "../audiomenu.h"
#include "../spicelogging.h"
#include "../browserlogging.h"
#include "../sharedfolder.h"
#include "certificatestore.h"
#include "downloadpolicy.h"
#include "framesnapshot.h"
#include "../viewersettings.h"
#include "../scalingmenu.h"
#include "../inputmenu.h"
#include "../browserproxy.h"
#include "../../src/core/tunnelproxy.h"
#include <QtSpice/QSpiceConnection.h>
#include <QtSpice/QSpiceSession.h>
#include <QtSpice/QSpiceWidget.h>
#include <QApplication>
#include <QLoggingCategory>
#include <QDebug>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QMenuBar>
#include <QWebEngineCertificateError>
#include <QWebEngineDownloadRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

class ConsoleWindow final : public QMainWindow {
public:
    explicit ConsoleWindow(const QtSpice::ConnectionConfig &config) : session_(this) {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(QStringLiteral("QtSpice Console"));
        auto *widget = new QtSpice::Widget(this);
        widget->setSession(&session_);
        setCentralWidget(widget);
        AudioMenu::add(menuBar(), &session_);
        SharedFolder::addMenu(menuBar(), &session_);
        auto *viewMenu = menuBar()->addMenu(QStringLiteral("View"));
        addScalingMenu(viewMenu, widget);
        addInputMenu(menuBar(), statusBar(), widget);
        auto *snapshotAction = viewMenu->addAction(QStringLiteral("Save Next Frame…"));
        connect(snapshotAction, &QAction::triggered, this, [this] {
            const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save next guest frame"),
                QStringLiteral("frame.png"), QStringLiteral("PNG image (*.png)"));
            if (path.isEmpty()) return;
            snapshot_.arm(path);
            statusBar()->showMessage(QStringLiteral("Waiting for the next guest frame…"));
        });
        connect(&snapshot_, &FrameSnapshot::pendingChanged, snapshotAction, [snapshotAction](bool pending) {
            snapshotAction->setEnabled(!pending);
        });
        connect(&session_, &QtSpice::Session::frameReady, &snapshot_, &FrameSnapshot::capture);
        connect(&snapshot_, &FrameSnapshot::saved, this, [this](const QString &path) {
            statusBar()->showMessage(QStringLiteral("Frame saved to %1").arg(path));
        });
        connect(&snapshot_, &FrameSnapshot::failed, this, [this](const QString &message) {
            statusBar()->showMessage(message);
            QMessageBox::warning(this, QStringLiteral("Snapshot"), message);
        });
        auto *resizeAction = viewMenu->addAction(QStringLiteral("Auto Resize Guest"));
        resizeAction->setCheckable(true);
        resizeAction->setChecked(ViewerSettings::autoResizeGuest());
        widget->setAutoResizeGuest(resizeAction->isChecked());
        connect(resizeAction, &QAction::toggled, widget, [widget](bool enabled) {
            widget->setAutoResizeGuest(enabled);
            ViewerSettings::setAutoResizeGuest(enabled);
        });
        connect(&session_, &QtSpice::Session::errorOccurred, this, [this](const QString &message) { statusBar()->showMessage(message); });
        connect(&session_, &QtSpice::Session::stateChanged, this, [this](QtSpice::Session::State state) {
            if (state == QtSpice::Session::State::Disconnected) snapshot_.cancel();
            if (state == QtSpice::Session::State::Connected)
                statusBar()->showMessage(session_.mouseMode() == QtSpice::Session::MouseMode::Server
                    ? QStringLiteral("Connected · Relative mouse: click console to capture; Ctrl+Alt+G releases it")
                    : QStringLiteral("Connected · Absolute mouse mode"));
        });
        connect(&session_, &QtSpice::Session::mouseModeChanged, this, [this](QtSpice::Session::MouseMode mode) {
            if (mode == QtSpice::Session::MouseMode::Server)
                statusBar()->showMessage(QStringLiteral("Relative mouse: click console to capture; Ctrl+Alt+G releases it"));
            else statusBar()->showMessage(QStringLiteral("Absolute mouse mode"));
        });
        connect(&session_, &QtSpice::Session::clipboardReadyChanged, this, [this](bool ready) {
            if (session_.state() == QtSpice::Session::State::Connected)
                statusBar()->showMessage(ready ? QStringLiteral("Connected · Clipboard ready")
                    : QStringLiteral("Connected · Clipboard unavailable: guest agent disconnected or lacks support"));
        });
        resize(1100, 800);
        show();
        session_.connectToConfig(config);
    }
private:
    QtSpice::Session session_;
    FrameSnapshot snapshot_;
};

class BrowserWindow final : public QMainWindow {
public:
    explicit BrowserWindow(const QUrl &initialUrl, const QUrl &networkProxy, std::optional<QtSpice::SharedDirectory> share) : sharedDirectory_(share), networkProxy_(networkProxy) {
        setWindowTitle(QStringLiteral("QtSpice Proxmox Viewer"));
        profile_ = new QWebEngineProfile(QStringLiteral("QtSpiceProxmox"), qApp);
        profile_->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
        view_ = new QWebEngineView(this);
        view_->setPage(new QWebEnginePage(profile_, view_));
        setCentralWidget(view_);
        auto *bar = addToolBar(QStringLiteral("Navigation"));
        bar->addAction(QStringLiteral("Back"), view_, &QWebEngineView::back);
        bar->addAction(QStringLiteral("Forward"), view_, &QWebEngineView::forward);
        bar->addAction(QStringLiteral("Reload"), view_, &QWebEngineView::reload);
        auto *address = new QLineEdit(this);
        bar->addWidget(address);
        connect(address, &QLineEdit::returnPressed, this, [this, address] {
            const QUrl url = QUrl::fromUserInput(address->text());
            if (url.scheme() == QStringLiteral("https")) view_->load(url);
            else statusBar()->showMessage(QStringLiteral("Enter an HTTPS Proxmox URL"));
        });
        connect(view_, &QWebEngineView::urlChanged, this, [address](const QUrl &url) { address->setText(url.toString()); });
        connect(view_->page(), &QWebEnginePage::certificateError, this, [this](const QWebEngineCertificateError &error) {
            auto decision = error;
            const auto chain = error.certificateChain();
            const QByteArray leaf = chain.isEmpty() ? QByteArray() : chain.first().toDer();
            const int type = int(error.type());
            if (!error.isOverridable()) {
                decision.rejectCertificate();
                return;
            }
            if (!leaf.isEmpty() && trust_.isTrusted(error.url(), leaf, type)) {
                decision.acceptCertificate();
                return;
            }
            if (!error.isMainFrame() || leaf.isEmpty()) {
                decision.rejectCertificate();
                return;
            }
            decision.defer();
            QTimer::singleShot(0, this, [this, decision, leaf, type]() mutable {
                QMessageBox box(QMessageBox::Warning, QStringLiteral("Certificate warning"),
                    QStringLiteral("The certificate for %1 has a warning:\n%2\n\nSHA-256: %3\n\nContinue only if you trust this Proxmox server.")
                        .arg(decision.url().authority(), decision.description(), CertificateStore::fingerprint(leaf)),
                    QMessageBox::Cancel, this);
                auto *continueButton = box.addButton(QStringLiteral("Continue"), QMessageBox::AcceptRole);
                box.exec();
                if (box.clickedButton() == continueButton) {
                    trust_.remember(decision.url(), leaf, type);
                    decision.acceptCertificate();
                } else {
                    decision.rejectCertificate();
                }
            });
        });
        connect(profile_, &QWebEngineProfile::downloadRequested, this, [this](QWebEngineDownloadRequest *request) {
            if (!isVvDownload(request->downloadFileName())) {
                const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save download"), request->downloadFileName());
                if (path.isEmpty()) { request->cancel(); return; }
                const QFileInfo info(path);
                request->setDownloadDirectory(info.absolutePath());
                request->setDownloadFileName(info.fileName());
                request->accept();
                return;
            }
            const QString path = temp_.filePath(QStringLiteral("console-%1.vv").arg(request->id()));
            request->setDownloadDirectory(temp_.path());
            request->setDownloadFileName(QFileInfo(path).fileName());
            connect(request, &QWebEngineDownloadRequest::stateChanged, this, [this, request, path](QWebEngineDownloadRequest::DownloadState state) {
                if (state == QWebEngineDownloadRequest::DownloadCompleted) {
                    QFile file(path);
                    QString error;
                    if (!file.open(QIODevice::ReadOnly)) error = QStringLiteral("Could not read SPICE connection download");
                    else {
                        auto config = QtSpice::parseVv(file.read(1024 * 1024 + 1), &error);
                        file.close();
                        QFile::remove(path);
                        if (config) {
                            config->networkProxy = networkProxy_.toString(QUrl::FullyEncoded);
                            config->sharedDirectory = sharedDirectory_;
                            new ConsoleWindow(*config);
                            return;
                        }
                    }
                    QMessageBox::warning(this, QStringLiteral("SPICE connection"), error);
                } else if (state == QWebEngineDownloadRequest::DownloadInterrupted) {
                    QFile::remove(path);
                    statusBar()->showMessage(QStringLiteral("SPICE connection download failed"));
                }
            });
            request->accept();
        });
        resize(1280, 850);
        view_->load(initialUrl);
    }
private:
    QWebEngineProfile *profile_ = nullptr;
    QWebEngineView *view_ = nullptr;
    QTemporaryDir temp_;
    CertificateStore trust_;
    std::optional<QtSpice::SharedDirectory> sharedDirectory_;
    QUrl networkProxy_;
};

int main(int argc, char **argv) {
    configureBrowserLogging();
    QApplication app(argc, argv);
    SpiceLogging::configure();
    QCoreApplication::setOrganizationName(QStringLiteral("QtSpice"));
    QCoreApplication::setApplicationName(QStringLiteral("ProxmoxViewer"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("performance-stats"), QStringLiteral("Enable one-second performance diagnostics")});
    SharedFolder::addOptions(parser);
    QCommandLineOption proxy(QStringLiteral("proxy"), QStringLiteral("Proxy for browser and consoles: http://host:port or socks5://host:port"), QStringLiteral("url"));
    parser.addOption(proxy);
    parser.addPositionalArgument(QStringLiteral("url"), QStringLiteral("Proxmox HTTPS URL"));
    parser.process(app);
    if (parser.isSet(QStringLiteral("performance-stats")))
        QLoggingCategory::setFilterRules(QStringLiteral("qtspice.performance.debug=true"));
    std::optional<QtSpice::SharedDirectory> sharedDirectory;
    QString sharingError;
    if (!SharedFolder::parse(parser, sharedDirectory, sharingError)) { qCritical().noquote() << sharingError; return 1; }
    if (parser.positionalArguments().size() != 1) parser.showHelp(1);
    const QUrl url = QUrl::fromUserInput(parser.positionalArguments().first());
    if (url.scheme() != QStringLiteral("https") || url.host().isEmpty()) parser.showHelp(1);
    QUrl networkProxy;
    QtSpice::TunnelProxy browserProxy;
    if (parser.isSet(proxy)) {
        QString error;
        const auto parsed = QtSpice::parseProxyUrl(parser.value(proxy), &error);
        if (!parsed) { qCritical().noquote() << error; return 1; }
        networkProxy = *parsed;
        if (!browserProxy.start(networkProxy)) {
            qCritical() << "Could not start local browser proxy bridge";
            return 1;
        }
        configureBrowserProxy(browserProxy.url());
    }
    BrowserWindow window(url, networkProxy, sharedDirectory);
    window.show();
    return app.exec();
}
