#include "clipboardbridge.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QtTest>
#include <QMimeData>
#include "../src/core/clipboardtransfer.h"

class ClipboardBridgeTest : public QObject {
    Q_OBJECT
private slots:
    void codecs() {
        for (auto type : {2u, 3u, 4u, 5u}) {
            const auto format = QtSpice::clipboardFormat(type);
            if (!QImageReader::supportedImageFormats().contains(format) || !QImageWriter::supportedImageFormats().contains(format)) {
                QVERIFY(type != 2); continue; // Optional runtime codec absent.
            }
            QFile file(QFINDTESTDATA(qPrintable("data/clipboard/sample." + (type == 5 ? QByteArray("jpg") : format))));
            QVERIFY(file.open(QIODevice::ReadOnly));
            auto image = QtSpice::decodeClipboardImage(file.readAll(), format);
            QVERIFY2(!image.isNull(), format.constData()); QCOMPARE(image.size(), QSize(12, 8));
            const auto encoded = QtSpice::encodeClipboardImage(image, type);
            QVERIFY(!encoded.isEmpty());
            const auto decoded = QtSpice::decodeClipboardImage(encoded, format);
            QCOMPARE(decoded.size(), image.size());
            if (type != 5) QCOMPARE(decoded.convertToFormat(QImage::Format_RGB32), image.convertToFormat(QImage::Format_RGB32));
        }
        QVERIFY(QtSpice::decodeClipboardImage("broken", "png").isNull());
        QVERIFY(!QtSpice::clipboardImageSize(QSize(INT_MAX, INT_MAX)));
        QVERIFY(QtSpice::clipboardImageSize(QSize(8192,8192)));
        QVERIFY(!QtSpice::clipboardImageSize(QSize(8193,8192)));
        QImage transparent(2, 2, QImage::Format_ARGB32); transparent.fill(Qt::transparent);
        QCOMPARE(QtSpice::decodeClipboardImage(QtSpice::encodeClipboardImage(transparent, 2), "png"), transparent);
        auto jpeg = QtSpice::decodeClipboardImage(QtSpice::encodeClipboardImage(transparent, 5), "jpeg");
        QVERIFY(jpeg.pixelColor(0,0).red() > 245);
    }
    void protocolAndMixedContent() {
        QtSpice::ClipboardTransfer transfer;
        QList<quint32> offered, requests;
        QList<QtSpice::ClipboardContent> received;
        QList<unsigned> replies;
        transfer.grab = [&](auto types) { offered = types; };
        transfer.release = [] {};
        transfer.request = [&](auto type) { requests << type; };
        transfer.notify = [&](auto type, auto) { replies << type; };
        transfer.received = [&](auto value) { received << value; };
        transfer.reset(true);
        QImage image(4,4,QImage::Format_ARGB32); image.fill(Qt::red);
        transfer.setHost({QStringLiteral("caption"), image});
        QVERIFY(offered.contains(1)); QVERIFY(offered.contains(2));
        QVERIFY(transfer.hostRequest(2)); QTRY_COMPARE(replies.size(), 1); QCOMPARE(replies.first(), 2u);
        QVERIFY(transfer.hostRequest(99)); QCOMPARE(replies.last(), 0u);
        QVERIFY(transfer.offer({1,2,3})); QTRY_COMPARE(requests.size(),1); QCOMPARE(requests.last(),1u);
        transfer.data(1,"caption"); QCOMPARE(received.size(),1); QCOMPARE(requests.last(),2u);
        transfer.data(2,"invalid png"); QTRY_COMPARE(requests.last(),3u);
        transfer.data(3,QtSpice::encodeClipboardImage(image,3)); QTRY_COMPARE(received.size(),2);
        QCOMPARE(received.last().text.value(), QStringLiteral("caption")); QCOMPARE(received.last().image.size(), QSize(4,4));
        transfer.offer({2}); QTRY_COMPARE(requests.size(),4);
        transfer.data(2,QtSpice::encodeClipboardImage(image,2));
        transfer.setHost({QStringLiteral("newer"), {}});
        QTest::qWait(50); QCOMPARE(received.size(),2);
        transfer.reset(false); QVERIFY(!transfer.hostRequest(1)); QVERIFY(!transfer.offer({2}));
    }
    void ownershipAndLimits() {
        QtSpice::ClipboardTransfer transfer;
        QList<unsigned> requests;
        QList<QtSpice::ClipboardContent> received;
        QList<quint32> offered;
        int releases = 0;
        transfer.grab = [&](auto types) { offered = types; };
        transfer.release = [&] { ++releases; };
        transfer.request = [&](auto type) { requests << type; };
        transfer.notify = [](auto, auto) {};
        transfer.received = [&](auto value) { received << value; };
        transfer.reset(true);
        transfer.offer({1}); QTRY_COMPARE(requests.size(),1);
        transfer.offer({1}); QTest::qWait(10); QCOMPARE(requests.size(),1);
        transfer.data(1,"stale"); QCOMPARE(received.size(),0); QCOMPARE(requests.size(),2);
        transfer.data(1,"fresh"); QCOMPARE(received.size(),1); QCOMPARE(received.last().text.value(),QStringLiteral("fresh"));
        transfer.offer({1}); QTRY_COMPARE(requests.size(),3);
        transfer.guestRelease(); transfer.data(1,"released"); QCOMPARE(received.size(),1);
        QImage image(2,2,QImage::Format_ARGB32); image.fill(Qt::blue);
        transfer.writers = [] { return QList<QByteArray>{}; };
        transfer.setHost({QStringLiteral("text"),image}); QCOMPARE(offered,QList<quint32>{1});
        transfer.setHost({}); QCOMPARE(releases,1);
        transfer.readers = [] { return QList<QByteArray>{}; };
        QVERIFY(!transfer.offer({2,3,4,5}));
        transfer.readers = [] { return QImageReader::supportedImageFormats(); };
        transfer.offer({2}); QTRY_COMPARE(requests.size(),4);
        transfer.data(2,QByteArray(QtSpice::clipboardImageLimit + 1,'x')); QCOMPARE(received.size(),1);
        transfer.setHost({QString(4 * 1024 * 1024 + 1, 'x'),{}}); QVERIFY(!transfer.hostRequest(1));
    }
    void mixedBridge() {
        if (!QImageReader::supportedImageFormats().contains("webp")) QSKIP("Optional WebP codec unavailable");
        QtSpice::ClipboardBridge bridge;
        bridge.setActive(true);
        QSignalSpy host(&bridge, &QtSpice::ClipboardBridge::hostContentChanged);
        QImage image(3,2,QImage::Format_ARGB32); image.fill(Qt::green);
        bridge.applyGuestContent({QStringLiteral("caption"), image});
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("caption"));
        QVERIFY(QGuiApplication::clipboard()->mimeData()->hasImage()); QCOMPARE(host.size(),0);
        auto *mime = new QMimeData;
        QFile file(QFINDTESTDATA("data/clipboard/sample.webp")); QVERIFY(file.open(QIODevice::ReadOnly));
        mime->setData("image/webp",file.readAll()); mime->setText("webp caption");
        QGuiApplication::clipboard()->setMimeData(mime);
        QTRY_COMPARE(host.size(),1);
        auto value = qvariant_cast<QtSpice::ClipboardContent>(host.first().first());
        QCOMPARE(value.image.size(),QSize(12,8)); QCOMPARE(value.text.value(),QStringLiteral("webp caption"));
    }
    void hostAndGuestChanges() {
        QtSpice::ClipboardBridge bridge;
        QSignalSpy host(&bridge, &QtSpice::ClipboardBridge::hostTextChanged);
        QGuiApplication::clipboard()->setText(QStringLiteral("before"));
        bridge.setActive(true);
        QCOMPARE(host.size(), 1);
        QCOMPARE(host.takeFirst().at(0).toString(), QStringLiteral("before"));
        QGuiApplication::clipboard()->setText(QStringLiteral("høst → guest"));
        QCOMPARE(host.size(), 1);
        QCOMPARE(host.takeFirst().at(0).toString(), QStringLiteral("høst → guest"));
        bridge.applyGuestText(QStringLiteral("guest → host"));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("guest → host"));
        QCOMPARE(host.size(), 0);
        bridge.setActive(false);
        QGuiApplication::clipboard()->setText(QStringLiteral("after"));
        QCOMPARE(host.size(), 0);
        bridge.applyGuestText(QStringLiteral("ignored"));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("after"));
        bridge.setActive(true);
        QCOMPARE(host.size(), 1);
        QCOMPARE(host.takeFirst().at(0).toString(), QStringLiteral("after"));
        bridge.applyGuestText(QString());
        QCOMPARE(QGuiApplication::clipboard()->text(), QString());
        QCOMPARE(host.size(), 0);
    }
};
QTEST_MAIN(ClipboardBridgeTest)
#include "clipboardbridge_test.moc"
