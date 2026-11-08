// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <memory>

class FakeProxy : public QTcpServer {
public:
    bool socks = true;
    bool authenticate = false;
    bool reject = false;
    bool chain = false;
    bool stall = false;
    bool echo = true;
    QString destination;
    QByteArray credentials;
    QByteArray connectHeader;
    QByteArray payload;
    int requests = 0;
    FakeProxy() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                auto phase = std::make_shared<int>(socks ? 0 : 3);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, phase] {
                    buffer->append(socket->readAll());
                    if (stall) return;
                    if (*phase == 0) {
                        if (buffer->size() < 2 || buffer->size() < 2 + quint8(buffer->at(1))) return;
                        buffer->remove(0, 2 + quint8(buffer->at(1)));
                        socket->write(authenticate ? QByteArray::fromHex("0502") : QByteArray::fromHex("0500"));
                        *phase = authenticate ? 1 : 2;
                    }
                    if (*phase == 1) {
                        if (buffer->size() < 2) return;
                        const int userLength = quint8(buffer->at(1));
                        if (buffer->size() < 3 + userLength) return;
                        const int size = 3 + userLength + quint8(buffer->at(2 + userLength));
                        if (buffer->size() < size) return;
                        credentials = buffer->mid(2, userLength) + ':' + buffer->mid(3 + userLength, size - 3 - userLength);
                        buffer->remove(0, size);
                        socket->write(QByteArray::fromHex("0100"));
                        *phase = 2;
                    }
                    if (*phase == 2) {
                        if (buffer->size() < 5) return;
                        const int type = quint8(buffer->at(3));
                        const int length = type == 3 ? quint8(buffer->at(4)) : type == 1 ? 4 : 16;
                        const int offset = type == 3 ? 5 : 4;
                        if (buffer->size() < offset + length + 2) return;
                        const auto host = buffer->mid(offset, length);
                        const int port = (quint8(buffer->at(offset + length)) << 8) | quint8(buffer->at(offset + length + 1));
                        destination = QString::fromUtf8(host) + ':' + QString::number(port);
                        buffer->remove(0, offset + length + 2);
                        ++requests;
                        socket->write(QByteArray::fromHex(reject ? "05050001000000000000" : "05000001000000000000"));
                        if (reject) { socket->disconnectFromHost(); return; }
                        *phase = chain ? 3 : 4;
                    }
                    if (*phase == 3) {
                        const auto end = buffer->indexOf("\r\n\r\n");
                        if (end < 0) return;
                        connectHeader = buffer->left(end + 4);
                        buffer->remove(0, end + 4);
                        ++requests;
                        socket->write(reject ? "HTTP/1.1 407 Proxy Authentication Required\r\nContent-Length: 0\r\n\r\n"
                                             : "HTTP/1.1 200 Connection established\r\n\r\n");
                        if (reject) { socket->disconnectFromHost(); return; }
                        *phase = 4;
                    }
                    if (*phase == 4 && !buffer->isEmpty()) {
                        payload.append(*buffer);
                        if (echo) socket->write(*buffer);
                        buffer->clear();
                    }
                });
            }
        });
    }
    QUrl url() const {
        return QUrl(QStringLiteral("%1://127.0.0.1:%2").arg(socks ? QStringLiteral("socks5") : QStringLiteral("http")).arg(serverPort()));
    }
};

