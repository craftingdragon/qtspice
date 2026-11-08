// SPDX-License-Identifier: LGPL-3.0-or-later
#include "certificatestore.h"
#include "downloadpolicy.h"
#include <QTemporaryDir>
#include <QtTest>

class CertificateStoreTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void exactCertificateOriginAndWarning() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("trust.ini"));
        const QUrl first(QStringLiteral("https://pve-one.example:8006/#v1"));
        const QUrl sameOrigin(QStringLiteral("https://pve-one.example:8006/other"));
        const QUrl second(QStringLiteral("https://pve-two.example:8006/"));
        const QUrl otherPort(QStringLiteral("https://pve-one.example:8007/"));
        const QByteArray certA("certificate-a");
        const QByteArray certB("certificate-b");
        {
            CertificateStore store(path);
            QVERIFY(!store.isTrusted(first, certA, -202));
            store.remember(first, certA, -202);
            QVERIFY(store.isTrusted(sameOrigin, certA, -202));
            QVERIFY(!store.isTrusted(first, certA, -201));
            QVERIFY(!store.isTrusted(first, certB, -202));
            QVERIFY(!store.isTrusted(second, certA, -202));
            QVERIFY(!store.isTrusted(otherPort, certA, -202));
            store.remember(first, certA, -201);
            QVERIFY(store.isTrusted(first, certA, -202));
        }
        {
            CertificateStore store(path);
            QVERIFY(store.isTrusted(first, certA, -202));
            QVERIFY(store.isTrusted(first, certA, -201));
            store.remember(first, certB, -202);
            QVERIFY(!store.isTrusted(first, certA, -202));
            QVERIFY(store.isTrusted(first, certB, -202));
            QVERIFY(!store.isTrusted(first, certB, -201));
        }
    }
    void refusesInvalidOriginAndCertificate() {
        QTemporaryDir dir;
        CertificateStore store(dir.filePath(QStringLiteral("trust.ini")));
        store.remember(QUrl(QStringLiteral("http://pve.example")), QByteArray("cert"), -202);
        QVERIFY(!store.isTrusted(QUrl(QStringLiteral("http://pve.example")), QByteArray("cert"), -202));
        store.remember(QUrl(QStringLiteral("https://pve.example")), {}, -202);
        QVERIFY(!store.isTrusted(QUrl(QStringLiteral("https://pve.example")), {}, -202));
    }
    void classifiesVvDownloads() {
        QVERIFY(isVvDownload(QStringLiteral("pve-spice.vv")));
        QVERIFY(isVvDownload(QStringLiteral("PVE-SPICE.VV")));
        QVERIFY(!isVvDownload(QStringLiteral("report.pdf")));
        QVERIFY(!isVvDownload(QStringLiteral("../evil.vv")));
    }
};
QTEST_GUILESS_MAIN(CertificateStoreTest)
#include "certificatestore_test.moc"
