// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtSpice/QSpiceConnection.h>
#include <QtTest>

class ConnectionTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void proxyUrls_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<bool>("valid");
        QTest::newRow("http") << QStringLiteral("http://proxy.example:3128") << true;
        QTest::newRow("socks5") << QStringLiteral("socks5://proxy.example:1080") << true;
        QTest::newRow("ipv6") << QStringLiteral("socks5://[::1]:1080") << true;
        QTest::newRow("encoded-credentials") << QStringLiteral("socks5://user:p%40ss@proxy.example") << true;
        QTest::newRow("missing-scheme") << QStringLiteral("proxy.example:1080") << false;
        QTest::newRow("unsupported") << QStringLiteral("https://proxy.example") << false;
        QTest::newRow("no-host") << QStringLiteral("socks5://:1080") << false;
        QTest::newRow("zero-port") << QStringLiteral("socks5://proxy.example:0") << false;
        QTest::newRow("overflow-port") << QStringLiteral("socks5://proxy.example:65536") << false;
        QTest::newRow("path") << QStringLiteral("http://proxy.example/path") << false;
        QTest::newRow("query") << QStringLiteral("http://proxy.example?x") << false;
        QTest::newRow("fragment") << QStringLiteral("http://proxy.example#x") << false;
        QTest::newRow("nul") << QStringLiteral("socks5://user:p%00ss@proxy.example") << false;
    }
    void proxyUrls() {
        QFETCH(QString, input); QFETCH(bool, valid);
        QString error;
        const auto parsed = QtSpice::parseProxyUrl(input, &error);
        QCOMPARE(parsed.has_value(), valid);
        QCOMPARE(error.isEmpty(), valid);
    }
    void proxyDefaultsAndCredentials() {
        QCOMPARE(QtSpice::parseProxyUrl(QStringLiteral("http://proxy"))->port(), 8080);
        const auto parsed = QtSpice::parseProxyUrl(QStringLiteral("socks5://user:p%40ss@proxy"));
        QCOMPARE(parsed->port(), 1080);
        QCOMPARE(parsed->password(), QStringLiteral("p@ss"));
        QString error;
        QVERIFY(!QtSpice::parseProxyUrl(QStringLiteral("https://user:secret@proxy"), &error));
        QVERIFY(!error.contains(QStringLiteral("secret")));
    }
    void parsesProxyLayers() {
        const auto parsed = QtSpice::parseVv("[virt-viewer]\ntype=spice\nhost=guest.invalid\nport=5900\nproxy=http://pve.invalid:3128\nnetwork-proxy=socks5://localhost:1080\n");
        QVERIFY(parsed);
        QCOMPARE(parsed->proxy, QStringLiteral("http://pve.invalid:3128"));
        QCOMPARE(parsed->networkProxy, QStringLiteral("socks5://localhost:1080"));
        const auto legacy = QtSpice::parseVv("[virt-viewer]\ntype=spice\nhost=guest.invalid\ntls-port=61000\nproxy=pve.invalid:3128\n");
        QVERIFY(legacy);
        QCOMPARE(legacy->proxy, QStringLiteral("http://pve.invalid:3128"));
    }
    void parsesProxmoxFile() {
        QByteArray vv = "[virt-viewer]\r\ntype=spice\r\nhost=pve-ticket\r\ntls-port=61000\r\nproxy=http://pve.example:3128\r\npassword=secret\r\nhost-subject=CN=pve\r\nca=-----BEGIN CERTIFICATE-----\\nABC\\n-----END CERTIFICATE-----\r\n";
        QString error;
        auto config = QtSpice::parseVv(vv, &error);
        QVERIFY2(config.has_value(), qPrintable(error));
        QCOMPARE(config->host, QStringLiteral("pve-ticket"));
        QCOMPARE(config->tlsPort, quint16(61000));
        QCOMPARE(config->proxy, QStringLiteral("http://pve.example:3128"));
        QCOMPARE(config->password, QStringLiteral("secret"));
        QVERIFY(config->caPem.contains('\n'));
    }
    void rejectsMalformedFiles() {
        QString error;
        QVERIFY(!QtSpice::parseVv("[virt-viewer]\ntype=spice\nhost=x\n", &error));
        QCOMPARE(error, QStringLiteral("Missing port or tls-port"));
        QVERIFY(!QtSpice::parseVv("[virt-viewer]\ntype=spice\nhost=x\ntls-port=99999\n", &error));
        QCOMPARE(error, QStringLiteral("Invalid port"));
        QVERIFY(!QtSpice::parseVv("[virt-viewer]\ntype=vnc\nhost=x\nport=1\n", &error));
        QCOMPARE(error, QStringLiteral("Only SPICE connection files are supported"));
    }
    void doesNotLeakPasswordInError() {
        QString error;
        QVERIFY(!QtSpice::parseVv("[virt-viewer]\ntype=spice\nhost=x\npassword=secret\npassword=again\n", &error));
        QVERIFY(!error.contains(QStringLiteral("secret")));
        QVERIFY(!error.contains(QStringLiteral("again")));
    }
};
QTEST_GUILESS_MAIN(ConnectionTest)
#include "qspiceconnection_test.moc"
