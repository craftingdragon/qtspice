#include "tunnelproxy.h"
#include "proxyconnection.h"
#include "intervalstats.h"
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QTimer>
#include <unistd.h>

namespace QtSpice {
namespace {
constexpr qint64 bufferLimit = 256 * 1024;
class Tunnel final : public QObject {
public:
    Tunnel(QTcpSocket *client, const QUrl &outer, const QUrl &inner, QObject *parent)
        : QObject(parent), diagnostics_("proxy", this), client_(client), remote_(new QTcpSocket(this)), request_(new ProxyConnection(this)),
          outer_(outer), inner_(inner) {
        diagnostics_.updateGauges = [this] {
            diagnostics_.gauge(QStringLiteral("up-read-pending"), quint64(client_->bytesAvailable()));
            diagnostics_.gauge(QStringLiteral("up-write-pending"), quint64(remote_->bytesToWrite()));
            diagnostics_.gauge(QStringLiteral("down-read-pending"), quint64(remote_->bytesAvailable()));
            diagnostics_.gauge(QStringLiteral("down-write-pending"), quint64(client_->bytesToWrite()));
            diagnostics_.gauge(QStringLiteral("ready"), ready_);
        };
        client_->setParent(this);
        client_->setReadBufferSize(bufferLimit);
        remote_->setReadBufferSize(bufferLimit);
        remote_->setProxy(QNetworkProxy::NoProxy);
        connect(client_, &QTcpSocket::readyRead, this, [this] { if (ready_) pump(client_, remote_); else if (!connecting_) readHeader(); });
        connect(remote_, &QTcpSocket::readyRead, this, [this] { pump(remote_, client_); });
        connect(client_, &QTcpSocket::bytesWritten, this, [this](qint64 bytes) { diagnostics_.add(QStringLiteral("down-written-bytes"), quint64(bytes)); if (ready_) drainRemote(); });
        connect(remote_, &QTcpSocket::bytesWritten, this, [this](qint64 bytes) { diagnostics_.add(QStringLiteral("up-written-bytes"), quint64(bytes)); if (ready_) pump(client_, remote_); });
        connect(client_, &QTcpSocket::disconnected, this, [this] { deleteLater(); });
        connect(remote_, &QTcpSocket::disconnected, this, [this] {
            remoteEof_ = true;
            if (ready_) drainRemote(); else reject();
        });
        connect(client_, &QTcpSocket::errorOccurred, this, [this] { deleteLater(); });
        connect(remote_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
            if (error == QAbstractSocket::RemoteHostClosedError) return;
            if (ready_) client_->disconnectFromHost(); else reject();
        });
        connect(request_, &ProxyConnection::failed, this, [this] { reject(); });
        connect(request_, &ProxyConnection::socketReady, this, [this](int fd) {
            if (!remote_->setSocketDescriptor(fd)) { ::close(fd); reject(); return; }
            ready_ = true;
            request_->deleteLater();
            if (connectMethod_) client_->write("HTTP/1.1 200 Connection established\r\n\r\n");
            else remote_->write(forwardHeader_);
            pump(client_, remote_);
            pump(remote_, client_);
        });
        QTimer::singleShot(30000, this, [this] { if (!ready_) reject(); });
        if (client_->bytesAvailable()) readHeader();
    }
private:
    IntervalStats diagnostics_;
    QTcpSocket *client_;
    QTcpSocket *remote_;
    ProxyConnection *request_;
    QUrl outer_, inner_;
    bool ready_ = false;
    bool connecting_ = false;
    bool remoteEof_ = false;
    bool connectMethod_ = true;
    QByteArray forwardHeader_;
    void drainRemote() {
        pump(remote_, client_);
        if (remoteEof_ && !remote_->bytesAvailable()) client_->disconnectFromHost();
    }
    void reject() {
        if (client_->state() == QAbstractSocket::ConnectedState) {
            client_->write("HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            client_->disconnectFromHost();
        } else deleteLater();
    }
    void readHeader() {
        const auto header = client_->peek(16385);
        const auto end = header.indexOf("\r\n\r\n");
        if (end < 0) { if (header.size() > 16384) reject(); return; }
        const auto fields = header.left(header.indexOf("\r\n")).split(' ');
        if (fields.size() != 3 || (fields[2] != "HTTP/1.1" && fields[2] != "HTTP/1.0")) { reject(); return; }
        connectMethod_ = fields[0] == "CONNECT";
        QUrl destination;
        QString host;
        int port = 0;
        if (connectMethod_ && inner_.scheme() == QStringLiteral("http") && fields[1].startsWith("pvespiceproxy:")) {
            // Proxmox's host is an opaque signed ticket, not a URL hostname.
            // Only the final colon separates the transport port. Forward the
            // ticket unchanged to the configured HTTP proxy, never to DNS/SOCKS.
            const auto colon = fields[1].lastIndexOf(':');
            const auto portBytes = fields[1].mid(colon + 1);
            bool validPort = false;
            const uint number = portBytes.toUInt(&validPort);
            if (!validPort || !number || number > 65535) { reject(); return; }
            for (const unsigned char ch : portBytes)
                if (ch < '0' || ch > '9') { reject(); return; }
            const auto ticket = fields[1].left(colon);
            for (const unsigned char ch : ticket)
                if (ch <= 0x20 || ch >= 0x7f) { reject(); return; }
            host = QString::fromLatin1(ticket);
            port = int(number);
        } else {
            destination = QUrl((connectMethod_ ? QStringLiteral("tcp://") : QString()) + QString::fromLatin1(fields[1]), QUrl::StrictMode);
            port = destination.port(connectMethod_ ? -1 : 80);
            if (!destination.isValid() || destination.host().isEmpty() || port <= 0 ||
                !destination.userInfo().isEmpty() || destination.hasFragment() ||
                (connectMethod_ ? (!destination.path().isEmpty() || destination.hasQuery()) : destination.scheme() != QStringLiteral("http"))) {
                reject(); return;
            }
            host = destination.host();
        }
        if (!connectMethod_) {
            auto path = destination.path(QUrl::FullyEncoded).toLatin1();
            if (path.isEmpty()) path = "/";
            if (destination.hasQuery()) path += '?' + destination.query(QUrl::FullyEncoded).toLatin1();
            forwardHeader_ = fields[0] + ' ' + path + ' ' + fields[2] + "\r\n";
            const auto lines = header.left(end).split('\n');
            for (qsizetype i = 1; i < lines.size(); ++i) {
                auto line = lines[i];
                if (line.endsWith('\r')) line.chop(1);
                const auto name = line.left(line.indexOf(':')).trimmed().toLower();
                if (name != "proxy-authorization" && name != "proxy-connection" && name != "connection")
                    forwardHeader_ += line + "\r\n";
            }
            // One origin per connection, including redirects to another host.
            forwardHeader_ += "Connection: close\r\n\r\n";
        }
        client_->read(end + 4);
        connecting_ = true;
        request_->start(host, quint16(port), outer_, inner_);
    }
    void pump(QTcpSocket *from, QTcpSocket *to) {
        if (to->state() != QAbstractSocket::ConnectedState) return;
        const auto room = bufferLimit - to->bytesToWrite();
        if (room > 0 && from->bytesAvailable()) {
            const auto written = to->write(from->read(room));
            if (written > 0) diagnostics_.add(from == client_ ? QStringLiteral("up-forwarded-bytes") : QStringLiteral("down-forwarded-bytes"), quint64(written));
        }
    }
};
}

TunnelProxy::TunnelProxy(QObject *parent) : QObject(parent) {
    server_.setProxy(QNetworkProxy::NoProxy);
    connect(&server_, &QTcpServer::newConnection, this, [this] {
        while (server_.hasPendingConnections()) new Tunnel(server_.nextPendingConnection(), networkProxy_, tunnelProxy_, this);
    });
}
bool TunnelProxy::start(const QUrl &networkProxy, const QUrl &tunnelProxy) {
    networkProxy_ = networkProxy;
    tunnelProxy_ = tunnelProxy;
    return server_.isListening() || server_.listen(QHostAddress::LocalHost, 0);
}
QUrl TunnelProxy::url() const {
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort()));
}
}
