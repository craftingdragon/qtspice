#include "../src/core/tunnelproxy.h"
#include "../examples/browserproxy.h"
#include "fakeproxy.h"
#include <QApplication>
#include <QNetworkProxy>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QtTest>

class WebEngineProxyTest : public QObject {
    Q_OBJECT
    QtSpice::TunnelProxy bridge_;
private Q_SLOTS:
    void initTestCase() {
        QVERIFY(bridge_.start({}));
        configureBrowserProxy(bridge_.url());
    }
    void browserUsesProxy_data() {
        QTest::addColumn<bool>("socks");
        QTest::addColumn<bool>("auth");
        QTest::addColumn<QString>("host");
        QTest::newRow("socks5") << true << false << QStringLiteral("browser.invalid");
        QTest::newRow("socks5-auth") << true << true << QStringLiteral("browser.invalid");
        QTest::newRow("http") << false << false << QStringLiteral("browser.invalid");
        QTest::newRow("http-auth") << false << true << QStringLiteral("browser.invalid");
        QTest::newRow("socks5-loopback") << true << false << QStringLiteral("127.0.0.2");
        QTest::newRow("socks5-simple-hostname") << true << false << QStringLiteral("pve");
    }
    void browserUsesProxy() {
        QFETCH(bool, socks); QFETCH(bool, auth);
        QFETCH(QString, host);
        FakeProxy server; server.socks = socks; server.authenticate = auth; server.echo = false;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        auto upstream = server.url();
        if (auth) { upstream.setUserName(QStringLiteral("user")); upstream.setPassword(QStringLiteral("p@ss")); }
        QVERIFY(bridge_.start(upstream));
        // No real browser profile, external server, login, or certificate acknowledgment.
        QWebEngineProfile profile;
        QWebEnginePage page(&profile);
        page.load(QUrl(QStringLiteral("https://%1:8006").arg(host)));
        QTRY_VERIFY_WITH_TIMEOUT(!server.payload.isEmpty(), 10000);
        QCOMPARE(quint8(server.payload.at(0)), quint8(0x16)); // TLS ClientHello reaches the proxy.
        if (socks) {
            // SOCKS represents IP literals in binary; hostname case tests DNS delegation.
            if (host != QStringLiteral("127.0.0.2")) QCOMPARE(server.destination, host + QStringLiteral(":8006"));
            if (auth) QCOMPARE(server.credentials, QByteArray("user:p@ss"));
        } else {
            QVERIFY(server.connectHeader.startsWith("CONNECT browser.invalid:8006 HTTP/1."));
            if (auth) QVERIFY(server.connectHeader.contains("Proxy-Authorization: Basic dXNlcjpwQHNz"));
        }
        page.triggerAction(QWebEnginePage::Stop);
    }
};
QTEST_MAIN(WebEngineProxyTest)
#include "webengineproxy_test.moc"
