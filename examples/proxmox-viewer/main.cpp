#include "certificatestore.h"
#include "downloadpolicy.h"
#include <QtSpice/QSpiceConnection.h>
#include <QtSpice/QSpiceSession.h>
#include <QtSpice/QSpiceWidget.h>
#include <QApplication>
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
        connect(&session_, &QtSpice::Session::errorOccurred, this, [this](const QString &message) { statusBar()->showMessage(message); });
        connect(&session_, &QtSpice::Session::stateChanged, this, [this](QtSpice::Session::State state) {
            if (state == QtSpice::Session::State::Connected) statusBar()->showMessage(QStringLiteral("Connected"));
        });
        resize(1100, 800);
        show();
        session_.connectToConfig(config);
    }
private:
    QtSpice::Session session_;
};

class BrowserWindow final : public QMainWindow {
public:
    explicit BrowserWindow(const QUrl &initialUrl) {
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
                        const auto config = QtSpice::parseVv(file.read(1024 * 1024 + 1), &error);
                        file.close();
                        QFile::remove(path);
                        if (config) { new ConsoleWindow(*config); return; }
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
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QtSpice"));
    QCoreApplication::setApplicationName(QStringLiteral("ProxmoxViewer"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("url"), QStringLiteral("Proxmox HTTPS URL"));
    parser.process(app);
    if (parser.positionalArguments().size() != 1) parser.showHelp(1);
    const QUrl url = QUrl::fromUserInput(parser.positionalArguments().first());
    if (url.scheme() != QStringLiteral("https") || url.host().isEmpty()) parser.showHelp(1);
    BrowserWindow window(url);
    window.show();
    return app.exec();
}
