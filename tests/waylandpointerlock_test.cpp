// SPDX-License-Identifier: LGPL-3.0-or-later
#include "waylandpointerlock.h"

#include <QGuiApplication>
#include <QWindow>
#include <QtTest>

class WaylandPointerLockTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void lifecycle() {
        int motions = 0;
        int changes = 0;
        {
            QtSpice::WaylandPointerLock lock(
                [&](double, double) { ++motions; },
                [&](bool) { ++changes; });
            QVERIFY(!lock.request(nullptr));
            QVERIFY(!lock.locked());
            lock.release();
            lock.release();

            if (!QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
                QVERIFY(!lock.available());
                return;
            }

            QTRY_VERIFY_WITH_TIMEOUT(lock.available(), 3000);
            QWindow window;
            window.resize(100, 100);
            window.show();
            QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
            QVERIFY(lock.request(&window));
            QVERIFY(lock.request(&window));
            lock.release();
            lock.release();
            QVERIFY(!lock.locked());
        }
        QCOMPARE(motions, 0);
        QCOMPARE(changes, 0);
    }
};

QTEST_MAIN(WaylandPointerLockTest)
#include "waylandpointerlock_test.moc"
