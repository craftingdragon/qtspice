#include <QtSpice/QSpiceDisplay.h>
#include <QtTest>
#include "../src/core/displayframe.h"

class DisplayTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void damageCopies_data() {
        QTest::addColumn<int>("format");
        QTest::newRow("rgb565") << int(QImage::Format_RGB16);
        QTest::newRow("rgb32") << int(QImage::Format_RGB32);
        QTest::newRow("argb32") << int(QImage::Format_ARGB32);
    }
    void damageCopies() {
        QFETCH(int, format);
        QImage source(101, 80, QImage::Format(format)); source.fill(Qt::blue);
        QImage target(101, 80, QImage::Format(format)); target.fill(Qt::red);
        QRegion damage(QRect(3,4,5,6)); damage += QRect(90,70,2,3);
        const auto *storage = target.constBits();
        QCOMPARE(QtSpice::applyFrameDamage(target, source, damage), quint64((30+6)*source.depth()/8));
        QCOMPARE(target.constBits(), storage);
        QCOMPARE(target.pixelColor(3,4), QColor(Qt::blue));
        QCOMPARE(target.pixelColor(2,4), QColor(Qt::red));
        QCOMPARE(target.pixelColor(91,72), QColor(Qt::blue));
        auto retained = target;
        source.fill(Qt::green);
        QtSpice::applyFrameDamage(target, source, QRect(3,4,1,1));
        QCOMPARE(retained.pixelColor(3,4), QColor(Qt::blue));
        QCOMPARE(target.pixelColor(3,4), QColor(Qt::green));
        QImage replacement(12,10,QImage::Format_RGB32); replacement.fill(Qt::yellow);
        QtSpice::applyFrameDamage(target, replacement, replacement.rect());
        QCOMPARE(target, replacement);
        QtSpice::applyFrameDamage(target, {}, {}); QVERIFY(target.isNull());
    }
    void boundedRegions() {
        QCOMPARE(QtSpice::boundedDamage(QRect(-5,-5,8,8), QRect(0,0,100,100)), QRegion(QRect(0,0,3,3)));
        QVERIFY(QtSpice::boundedDamage(QRect(200,200,2,2), QRect(0,0,100,100)).isEmpty());
        QCOMPARE(QtSpice::boundedDamage(QRect(0,0,50,100), QRect(0,0,100,100)), QRegion(QRect(0,0,100,100)));
        QRegion fragmented;
        for (int i=0;i<70;++i) fragmented += QRect(i*2,0,1,1);
        QCOMPARE(QtSpice::boundedDamage(fragmented,QRect(0,0,200,100)).rectCount(),1);
    }

    void cropsIndependentHeads() {
        QImage backing(100, 40, QImage::Format_RGB32); backing.fill(Qt::red);
        for (int y = 0; y < 40; ++y) for (int x = 50; x < 100; ++x) backing.setPixelColor(x, y, Qt::blue);
        SpiceDisplayPrimary p{}; p.format = SPICE_SURFACE_FMT_32_xRGB; p.width = 100; p.height = 40;
        p.stride = backing.bytesPerLine(); p.data = backing.bits();
        auto left = QtSpice::copyMonitorRect(p, {0, 0, 50, 40});
        auto right = QtSpice::copyMonitorRect(p, {50, 0, 50, 40});
        QCOMPARE(left.pixelColor(49,39), QColor(Qt::red));
        QCOMPARE(right.pixelColor(0,0), QColor(Qt::blue));
        backing.fill(Qt::green);
        QCOMPARE(right.pixelColor(0,0), QColor(Qt::blue));
        QVERIFY(QtSpice::copyMonitorRect(p, {90,0,20,40}).isNull());
        QVERIFY(QtSpice::copyMonitorRect(p, {-1,0,20,40}).isNull());
    }
    void copiesPrimary() {
        quint32 pixels[] = {0x00112233, 0x00445566};
        QImage image = QtSpice::copyPrimary(32, 2, 1, 8, reinterpret_cast<const uchar *>(pixels));
        QCOMPARE(image.size(), QSize(2, 1));
        pixels[0] = 0;
        QCOMPARE(image.pixel(0, 0), QRgb(0xff112233));
    }
    void monitorExcludesStaleRows() {
        QImage surface(64, 64, QImage::Format_RGB32);
        surface.fill(Qt::red); // Old pixels retained in the backing allocation.
        for (int y = 0; y < 60; ++y)
            for (int x = 0; x < 64; ++x) surface.setPixelColor(x, y, Qt::green);
        SpiceDisplayPrimary primary{};
        primary.format = SPICE_SURFACE_FMT_32_xRGB;
        primary.width = surface.width(); primary.height = surface.height();
        primary.stride = surface.bytesPerLine(); primary.data = surface.bits();
        GArray *monitors = g_array_new(FALSE, FALSE, sizeof(SpiceDisplayMonitorConfig));
        SpiceDisplayMonitorConfig monitor{};
        monitor.width = 64; monitor.height = 60;
        g_array_append_val(monitors, monitor);
        QImage frame = QtSpice::copyMonitor(primary, monitors);
        QCOMPARE(frame.size(), QSize(64, 60));
        QCOMPARE(frame.pixelColor(63, 59), QColor(Qt::green));
        // A monitor update without a new surface must change the visible size.
        g_array_index(monitors, SpiceDisplayMonitorConfig, 0).height = 58;
        QCOMPARE(QtSpice::copyMonitor(primary, monitors).size(), QSize(64, 58));
        auto &config = g_array_index(monitors, SpiceDisplayMonitorConfig, 0);
        config.x = 4; config.y = 2; config.width = 60; config.height = 58;
        QCOMPARE(QtSpice::copyMonitor(primary, monitors).size(), QSize(60, 58));
        config.width = G_MAXUINT;
        QVERIFY(QtSpice::copyMonitor(primary, monitors).isNull());
        config.width = 0;
        QVERIFY(QtSpice::copyMonitor(primary, monitors).isNull());
        QCOMPARE(QtSpice::copyMonitor(primary, nullptr).size(), surface.size());
        g_array_unref(monitors);
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
        QCOMPARE(QtSpice::nativeXtScanCode(0x1e + 8, Qt::Key_Q), quint32(0x1e));
        QCOMPARE(QtSpice::nativeXtScanCode(0x61 + 8, Qt::Key_Control), quint32(0x11d));
        QCOMPARE(QtSpice::nativeXtScanCode(0x64 + 8, Qt::Key_AltGr), quint32(0x138));
        QCOMPARE(QtSpice::nativeXtScanCode(0x60 + 8, Qt::Key_Enter), quint32(0x11c));
        QCOMPARE(QtSpice::nativeXtScanCode(0x67 + 8, Qt::Key_Up), quint32(0x148));
        QCOMPARE(QtSpice::nativeXtScanCode(0, Qt::Key_A), quint32(0x1e));
        QCOMPARE(QtSpice::nativeXtScanCode(9999, Qt::Key_A), quint32(0));
    }
};
QTEST_GUILESS_MAIN(DisplayTest)
#include "qspicedisplay_test.moc"
