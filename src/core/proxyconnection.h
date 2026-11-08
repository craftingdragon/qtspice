// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QUrl>
#include <gio/gio.h>

namespace QtSpice {
// One cancellable asynchronous TCP tunnel. Application TLS is passed through.
class ProxyConnection final : public QObject {
    Q_OBJECT
public:
    explicit ProxyConnection(QObject *parent = nullptr);
    ~ProxyConnection() override;
    void start(const QString &host, quint16 port, const QUrl &networkProxy, const QUrl &tunnelProxy = {});
Q_SIGNALS:
    // Ownership of the descriptor transfers to the receiver (direct connection).
    void socketReady(int fd);
    void failed(const QString &message);
private:
    GCancellable *cancel_;
    GSocketConnection *connection_ = nullptr;
    QString host_;
    quint16 port_ = 0;
    QUrl tunnelProxy_;
    bool finished_ = false;
    void connected(GSocketConnection *connection);
    void finish();
    void fail(const QString &message);
};
}
