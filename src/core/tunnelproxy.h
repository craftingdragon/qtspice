// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QObject>
#include <QTcpServer>
#include <QUrl>

namespace QtSpice {
// Private loopback HTTP/CONNECT bridge used by both libspice and WebEngine.
// Proxy negotiation is delegated to GIO; TLS passes through unchanged.
class TunnelProxy final : public QObject {
public:
    explicit TunnelProxy(QObject *parent = nullptr);
    // Update routes for future connections if already listening.
    bool start(const QUrl &networkProxy, const QUrl &tunnelProxy = {});
    QUrl url() const;
private:
    QTcpServer server_;
    QUrl networkProxy_;
    QUrl tunnelProxy_;
};
}
