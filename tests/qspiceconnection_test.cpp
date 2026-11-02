#include <QtSpice/QSpiceConnection.h>
#include <QtTest>

class ConnectionTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
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
