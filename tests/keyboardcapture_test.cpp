#include "keyboardcapture.h"
#include <QGuiApplication>
#include <QWindow>
#include <QtTest>
class KeyboardCaptureTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void lifecycle() {
        int changes = 0;
        QtSpice::KeyboardCapture capture([&](bool) { ++changes; });
        QVERIFY(!capture.request(nullptr));
        capture.release();
        capture.release();
        if (!QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
            QVERIFY(!capture.available());
            QCOMPARE(changes, 0);
            return;
        }
        QTRY_VERIFY_WITH_TIMEOUT(capture.available(), 3000);
        QWindow window;
        window.resize(100,100);
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
        QVERIFY(capture.request(&window));
        QVERIFY(capture.request(&window));
        capture.release();
        capture.release();
    }
};
QTEST_MAIN(KeyboardCaptureTest)
#include "keyboardcapture_test.moc"
