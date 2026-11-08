// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../src/core/proxyconnection.h"
#include "../src/core/tunnelproxy.h"
#include <QtSpice/QSpiceSession.h>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QtTest>
#include <unistd.h>

#include "fakeproxy.h"

class ProxyConnectionTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void tunnels_data() {
        QTest::addColumn<bool>("socks");
        QTest::addColumn<bool>("auth");
        QTest::addColumn<bool>("chain");
        QTest::newRow("socks5") << true << false << false;
        QTest::newRow("socks5-auth") << true << true << false;
        QTest::newRow("http") << false << false << false;
        QTest::newRow("http-auth") << false << true << false;
        QTest::newRow("socks5-proxmox") << true << true << true;
    }
    void tunnels() {
        QFETCH(bool, socks); QFETCH(bool, auth); QFETCH(bool, chain);
        FakeProxy server;
        server.socks = socks; server.authenticate = auth; server.chain = chain;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        auto url = server.url();
        if (auth) { url.setUserName(QStringLiteral("user")); url.setPassword(QStringLiteral("p@ss")); }
        QtSpice::ProxyConnection request;
        QSignalSpy ready(&request, &QtSpice::ProxyConnection::socketReady);
        QSignalSpy failed(&request, &QtSpice::ProxyConnection::failed);
        request.start(QStringLiteral("guest.invalid"), 61000, url,
                      chain ? QUrl(QStringLiteral("http://pve.invalid:3128")) : QUrl());
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
        QVERIFY(failed.isEmpty());
        QTcpSocket socket;
        socket.setProxy(QNetworkProxy::NoProxy);
        QVERIFY(socket.setSocketDescriptor(ready.first().first().toInt()));
        socket.write("channel-data");
        QTRY_COMPARE(socket.bytesAvailable(), qint64(12));
        QCOMPARE(socket.readAll(), QByteArray("channel-data"));
        if (socks) {
            QCOMPARE(server.destination, chain ? QStringLiteral("pve.invalid:3128") : QStringLiteral("guest.invalid:61000"));
            if (auth) QCOMPARE(server.credentials, QByteArray("user:p@ss"));
        }
        if (!socks || chain) QVERIFY(server.connectHeader.startsWith("CONNECT guest.invalid:61000 HTTP/1."));
        if (!socks && auth) QVERIFY(server.connectHeader.contains("Proxy-Authorization: Basic dXNlcjpwQHNz"));
        QCOMPARE(server.requests, chain ? 2 : 1);
    }
    void failures_data() {
        QTest::addColumn<bool>("socks");
        QTest::newRow("socks5-refused") << true;
        QTest::newRow("http-rejected") << false;
    }
    void failures() {
        QFETCH(bool, socks);
        FakeProxy server; server.socks = socks; server.reject = true;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QtSpice::ProxyConnection request;
        QSignalSpy ready(&request, &QtSpice::ProxyConnection::socketReady);
        QSignalSpy failed(&request, &QtSpice::ProxyConnection::failed);
        auto url = server.url(); url.setUserName(QStringLiteral("secret-user")); url.setPassword(QStringLiteral("secret-password"));
        request.start(QStringLiteral("guest.invalid"), 5900, url);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QVERIFY(ready.isEmpty());
        QVERIFY(!failed.first().first().toString().contains(QStringLiteral("secret")));
    }
    void cancellationDuringNegotiation() {
        FakeProxy server; server.stall = true;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        for (int i = 0; i < 10; ++i) {
            auto request = std::make_unique<QtSpice::ProxyConnection>();
            request->start(QStringLiteral("guest.invalid"), 5900, server.url());
            QCoreApplication::processEvents();
        }
        QTest::qWait(50); // Deliver canceled GLib callbacks after their QObject has gone.
    }
    void sessionUsesProxy_data() {
        QTest::addColumn<bool>("tls");
        QTest::addColumn<int>("route");
        QTest::newRow("plain-socks5") << false << 0;
        QTest::newRow("tls-socks5") << true << 0;
        QTest::newRow("plain-vv-http") << false << 1;
        QTest::newRow("tls-vv-http") << true << 1;
        QTest::newRow("plain-socks5-proxmox") << false << 2;
        QTest::newRow("tls-socks5-proxmox") << true << 2;
    }
    void sessionUsesProxy() {
        QFETCH(bool, tls);
        QFETCH(int, route);
        FakeProxy server; server.echo = false;
        server.socks = route != 1;
        server.chain = route == 2;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QtSpice::Session session;
        QtSpice::ConnectionConfig config;
        config.host = QStringLiteral("guest.invalid");
        config.port = tls ? 0 : 5900;
        config.tlsPort = tls ? 61000 : 0;
        if (route == 1) config.proxy = server.url().toString();
        else config.networkProxy = server.url().toString();
        if (route == 2) config.proxy = QStringLiteral("http://pve.invalid:3128");
        session.connectToConfig(config);
        QTRY_VERIFY_WITH_TIMEOUT(!server.payload.isEmpty(), 5000);
        if (route == 0) QCOMPARE(server.destination, tls ? QStringLiteral("guest.invalid:61000") : QStringLiteral("guest.invalid:5900"));
        if (route == 2) QCOMPARE(server.destination, QStringLiteral("pve.invalid:3128"));
        if (route != 0) QVERIFY(server.connectHeader.startsWith(tls ? "CONNECT guest.invalid:61000 " : "CONNECT guest.invalid:5900 "));
        if (tls) QCOMPARE(quint8(server.payload.at(0)), quint8(0x16));
        else QVERIFY(server.payload.startsWith("REDQ"));
        session.disconnectFromHost();
    }
    void bridgeRoundTrip() {
        FakeProxy server; server.authenticate = true;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        auto upstream = server.url();
        upstream.setUserName(QStringLiteral("user")); upstream.setPassword(QStringLiteral("p@ss"));
        QtSpice::TunnelProxy bridge;
        QVERIFY(bridge.start(upstream));
        QTcpSocket client;
        client.setProxy(QNetworkProxy::NoProxy);
        client.connectToHost(bridge.url().host(), quint16(bridge.url().port()));
        QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
        // Simulate browser CONNECT with early data and a fragmented header.
        client.write("CONNECT web.invalid:8006 HTTP/1.1\r\nHost: web.invalid:8006\r\n");
        client.flush();
        QCoreApplication::processEvents();
        client.write("\r\nearly-data");
        QTRY_VERIFY(client.bytesAvailable() > 0);
        QByteArray received = client.readAll();
        QTRY_VERIFY((received += client.readAll()).contains("early-data"));
        QVERIFY(received.startsWith("HTTP/1.1 200"));
        QCOMPARE(server.destination, QStringLiteral("web.invalid:8006"));
        QCOMPARE(server.credentials, QByteArray("user:p@ss"));
        // Exercise bounded forwarding in both directions beyond bufferLimit.
        const QByteArray bulk(1024 * 1024, 'z');
        client.write(bulk);
        received.clear();
        QTRY_COMPARE_WITH_TIMEOUT((received += client.readAll()).size(), bulk.size(), 5000);
        QCOMPARE(received, bulk);
    }
    void proxmoxTicket_data() {
        QTest::addColumn<bool>("outerProxy");
        QTest::addColumn<bool>("signedPort");
        QTest::newRow("legacy-ticket-http") << false << false;
        QTest::newRow("ticket-http") << false << true;
        QTest::newRow("legacy-ticket-socks5-http") << true << false;
        QTest::newRow("ticket-socks5-http") << true << true;
    }
    void proxmoxTicket() {
        QFETCH(bool, outerProxy); QFETCH(bool, signedPort);
        FakeProxy server; server.echo = false; server.socks = outerProxy; server.chain = outerProxy;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        // Synthetic tickets shaped like Proxmox's .vv host, never real credentials.
        const QByteArray host = "pvespiceproxy:1234abcd:110:bad-apple" + QByteArray(signedPort ? ":61000" : "")
                              + "::0123456789abcdef0123456789abcdef01234567";
        const QByteArray vv = "[virt-viewer]\ntype=spice\nhost=" + host + "\ntls-port=61000\nproxy="
            + (outerProxy ? QByteArray("http://pve.invalid:3128") : server.url().toEncoded()) + '\n';
        auto config = QtSpice::parseVv(vv);
        QVERIFY(config);
        if (outerProxy) config->networkProxy = server.url().toString();
        QtSpice::Session session;
        QSignalSpy errors(&session, &QtSpice::Session::errorOccurred);
        session.connectToConfig(*config);
        QTRY_VERIFY_WITH_TIMEOUT(!server.payload.isEmpty() || !errors.isEmpty(), 5000);
        QVERIFY2(!server.payload.isEmpty(), errors.isEmpty() ? "No TLS handshake" : qPrintable(errors.first().first().toString()));
        QVERIFY(server.connectHeader.startsWith("CONNECT " + host + ":61000 HTTP/1."));
        QVERIFY(server.connectHeader.contains("\r\nHost: " + host + ":61000\r\n"));
        QCOMPARE(quint8(server.payload.at(0)), quint8(0x16));
        if (outerProxy) QCOMPARE(server.destination, QStringLiteral("pve.invalid:3128"));
        session.disconnectFromHost();
    }
    void browserHttpForwarding() {
        FakeProxy server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QtSpice::TunnelProxy bridge;
        QVERIFY(bridge.start(server.url()));
        QTcpSocket client;
        client.setProxy(QNetworkProxy::NoProxy);
        client.connectToHost(bridge.url().host(), quint16(bridge.url().port()));
        QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
        client.write("GET http://web.invalid/path?q=value HTTP/1.1\r\nHost: web.invalid\r\nProxy-Authorization: secret\r\nProxy-Connection: keep-alive\r\n\r\n");
        QTRY_VERIFY(server.payload.contains("\r\n\r\n"));
        QCOMPARE(server.destination, QStringLiteral("web.invalid:80"));
        QVERIFY(server.payload.startsWith("GET /path?q=value HTTP/1.1\r\n"));
        QVERIFY(server.payload.contains("Connection: close\r\n"));
        QVERIFY(!server.payload.contains("secret"));
        QVERIFY(!server.payload.contains("Proxy-Connection"));
    }
    void bridgeRejectsFailedProxy() {
        FakeProxy server; server.reject = true;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QtSpice::TunnelProxy bridge;
        QVERIFY(bridge.start(server.url()));
        QTcpSocket client;
        client.setProxy(QNetworkProxy::NoProxy);
        client.connectToHost(bridge.url().host(), quint16(bridge.url().port()));
        QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
        client.write("CONNECT web.invalid:8006 HTTP/1.1\r\n\r\n");
        QTRY_VERIFY(client.bytesAvailable() > 0);
        QVERIFY(client.readAll().startsWith("HTTP/1.1 502"));
        QVERIFY(server.payload.isEmpty());
    }
};
QTEST_GUILESS_MAIN(ProxyConnectionTest)
#include "proxyconnection_test.moc"
