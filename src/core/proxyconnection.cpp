// SPDX-License-Identifier: LGPL-3.0-or-later
#include "proxyconnection.h"
#include <QPointer>
#include <QTimer>
#include <fcntl.h>
#include <unistd.h>
#include <memory>

namespace QtSpice {
ProxyConnection::ProxyConnection(QObject *parent) : QObject(parent), cancel_(g_cancellable_new()) {}
ProxyConnection::~ProxyConnection() {
    g_cancellable_cancel(cancel_);
    g_clear_object(&connection_);
    g_object_unref(cancel_);
}

void ProxyConnection::start(const QString &host, quint16 port, const QUrl &networkProxy, const QUrl &tunnelProxy) {
    host_ = host;
    port_ = port;
    tunnelProxy_ = tunnelProxy;
    auto *client = g_socket_client_new();
    g_socket_client_set_timeout(client, 30);
    // An explicit resolver prevents environment/system proxy fallback or bypass.
    const QByteArray proxy = networkProxy.toEncoded();
    auto *resolver = g_simple_proxy_resolver_new(proxy.isEmpty() ? "direct://" : proxy.constData(), nullptr);
    g_socket_client_set_proxy_resolver(client, resolver);
    g_object_unref(resolver);
    const QString endpoint = tunnelProxy.isEmpty() ? host : tunnelProxy.host();
    const quint16 endpointPort = tunnelProxy.isEmpty() ? port : quint16(tunnelProxy.port());
    // Bracket IPv6, and leave destination names for the proxy to resolve.
    const QByteArray address = (endpoint.contains(QLatin1Char(':'))
        ? QStringLiteral("[%1]:%2").arg(endpoint).arg(endpointPort)
        : QStringLiteral("%1:%2").arg(endpoint).arg(endpointPort)).toUtf8();
    g_socket_client_connect_to_host_async(client, address.constData(), endpointPort, cancel_,
        +[](GObject *source, GAsyncResult *result, gpointer data) {
            std::unique_ptr<QPointer<ProxyConnection>> guard(static_cast<QPointer<ProxyConnection> *>(data));
            GError *error = nullptr;
            auto *connection = g_socket_client_connect_to_host_finish(G_SOCKET_CLIENT(source), result, &error);
            if (*guard && !(*guard)->finished_) {
                if (connection) (*guard)->connected(connection);
                else (*guard)->fail(QStringLiteral("Could not connect through network proxy"));
            }
            if (connection) g_object_unref(connection);
            g_clear_error(&error);
        }, new QPointer<ProxyConnection>(this));
    g_object_unref(client);
    QTimer::singleShot(30000, this, [this] {
        if (!finished_) fail(QStringLiteral("Proxy connection timed out"));
    });
}

void ProxyConnection::connected(GSocketConnection *connection) {
    connection_ = G_SOCKET_CONNECTION(g_object_ref(connection));
    if (tunnelProxy_.isEmpty()) { finish(); return; }
    auto *proxy = g_proxy_get_default_for_protocol(tunnelProxy_.scheme().toUtf8().constData());
    if (!proxy) { fail(QStringLiteral("Proxy protocol is unavailable")); return; }
    // The actual connection is already established, possibly through SOCKS5.
    auto *dummy = g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
    const auto host = host_.toUtf8();
    const auto user = tunnelProxy_.userName().toUtf8();
    const auto password = tunnelProxy_.password().toUtf8();
    auto *address = g_proxy_address_new(dummy, guint16(tunnelProxy_.port()),
        tunnelProxy_.scheme().toUtf8().constData(), host.constData(), port_,
        user.isEmpty() ? nullptr : user.constData(), password.isEmpty() ? nullptr : password.constData());
    g_object_unref(dummy);
    g_proxy_connect_async(proxy, G_IO_STREAM(connection_), G_PROXY_ADDRESS(address), cancel_,
        +[](GObject *source, GAsyncResult *result, gpointer data) {
            std::unique_ptr<QPointer<ProxyConnection>> guard(static_cast<QPointer<ProxyConnection> *>(data));
            GError *error = nullptr;
            auto *stream = g_proxy_connect_finish(G_PROXY(source), result, &error);
            if (*guard && !(*guard)->finished_) {
                if (stream) (*guard)->finish();
                else (*guard)->fail(QStringLiteral("SPICE proxy tunnel was rejected"));
            }
            if (stream) g_object_unref(stream);
            g_clear_error(&error);
        }, new QPointer<ProxyConnection>(this));
    g_object_unref(address);
    g_object_unref(proxy);
}

void ProxyConnection::finish() {
    const int fd = fcntl(g_socket_get_fd(g_socket_connection_get_socket(connection_)), F_DUPFD_CLOEXEC, 0);
    if (fd < 0) { fail(QStringLiteral("Could not transfer proxy socket")); return; }
    finished_ = true;
    Q_EMIT socketReady(fd);
}

void ProxyConnection::fail(const QString &message) {
    if (finished_) return;
    finished_ = true;
    g_cancellable_cancel(cancel_);
    Q_EMIT failed(message);
}
}
