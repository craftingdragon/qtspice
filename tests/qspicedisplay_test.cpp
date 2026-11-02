#include <QtSpice/QSpiceDisplay.h>
#include <QtTest>

class DisplayTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void copiesPrimary() {
        quint32 pixels[] = {0x00112233, 0x00445566};
        QImage image = QtSpice::copyPrimary(32, 2, 1, 8, reinterpret_cast<const uchar *>(pixels));
        QCOMPARE(image.size(), QSize(2, 1));
        pixels[0] = 0;
        QCOMPARE(image.pixel(0, 0), QRgb(0xff112233));
    }
    void rejectsInvalidSurface() {
        quint32 pixel = 0;
        QVERIFY(QtSpice::copyPrimary(123, 1, 1, 4, reinterpret_cast<const uchar *>(&pixel)).isNull());
        QVERIFY(QtSpice::copyPrimary(32, 2, 1, 4, reinterpret_cast<const uchar *>(&pixel)).isNull());
    }
    void scalesCoordinates() {
        QCOMPARE(QtSpice::fittedRect(QSize(100, 100), QSize(200, 100)), QRect(50, 0, 100, 100));
        QCOMPARE(QtSpice::guestPoint(QPoint(100, 50), QSize(100, 100), QSize(200, 100)), QPoint(50, 50));
    }
    void mapsKeys() {
        QCOMPARE(QtSpice::xtScanCode(Qt::Key_A), quint32(0x1e));
        QCOMPARE(QtSpice::xtScanCode(Qt::Key_Up), quint32(0x148));
        QCOMPARE(QtSpice::xtScanCode(Qt::Key_unknown), quint32(0));
    }
};
QTEST_GUILESS_MAIN(DisplayTest)
#include "qspicedisplay_test.moc"
