// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../examples/proxmox-viewer/framesnapshot.h"
#include <QtSpice/QSpiceSession.h>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class FrameSnapshotTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void capturesNextFrameOnlyAndCanBeRearmed() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("frame.png"));
        FrameSnapshot snapshot;
        QSignalSpy saved(&snapshot, &FrameSnapshot::saved);
        QSignalSpy pending(&snapshot, &FrameSnapshot::pendingChanged);
        QtSpice::Session session;
        connect(&session, &QtSpice::Session::frameReady, &snapshot, &FrameSnapshot::capture);
        QImage frame(713, 419, QImage::Format_RGB32);
        frame.fill(Qt::black);
        session.frameReady(frame);
        QVERIFY(!QFile::exists(path));
        snapshot.arm(path);
        QCOMPARE(pending.count(), 1);
        QCOMPARE(pending.last().first().toBool(), true);
        session.frameReady({});
        QVERIFY(!QFile::exists(path));
        frame.fill(QColor(14, 52, 199));
        frame.setPixelColor(712, 418, Qt::red);
        session.frameReady(frame);
        QCOMPARE(saved.count(), 1);
        QCOMPARE(QImage(path).convertToFormat(frame.format()), frame);
        QCOMPARE(pending.last().first().toBool(), false);
        frame.fill(Qt::green);
        session.frameReady(frame);
        QCOMPARE(saved.count(), 1);
        QVERIFY(QImage(path).pixelColor(0, 0) != QColor(Qt::green));
        snapshot.arm(path);
        session.frameReady(frame);
        QCOMPARE(saved.count(), 2);
        QCOMPARE(QImage(path).convertToFormat(frame.format()), frame);
    }

    void cancellationAndIndependentConsoles() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("frame.png"));
        FrameSnapshot first, second;
        QSignalSpy saved(&first, &FrameSnapshot::saved);
        QImage frame(2, 2, QImage::Format_RGB32);
        frame.fill(Qt::black);
        first.arm({}); // Cancelled save dialog does not arm capture.
        first.capture(frame);
        QCOMPARE(saved.count(), 0);
        first.arm(path);
        second.capture(frame);
        QVERIFY(!QFile::exists(path));
        first.cancel();
        first.capture(frame);
        QVERIFY(!QFile::exists(path));
        first.arm(path);
        first.capture(frame);
        QCOMPARE(saved.count(), 1);
    }

    void failureAllowsAnotherRequest() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("missing/frame.png"));
        FrameSnapshot snapshot;
        QSignalSpy failed(&snapshot, &FrameSnapshot::failed);
        QSignalSpy saved(&snapshot, &FrameSnapshot::saved);
        QImage frame(2, 2, QImage::Format_RGB32);
        frame.fill(Qt::black);
        snapshot.arm(path);
        snapshot.capture(frame);
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed.first().first().toString().contains(path));
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("missing")));
        snapshot.capture(frame);
        QVERIFY(!QFile::exists(path));
        snapshot.arm(path);
        snapshot.capture(frame);
        QCOMPARE(saved.count(), 1);
    }
};
QTEST_GUILESS_MAIN(FrameSnapshotTest)
#include "framesnapshot_test.moc"
