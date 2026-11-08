// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../src/core/qspicesession.h"
#include "../examples/filesending.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
using namespace QtSpice;
using S = FileTransferInfo::State;
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
using DragPosition = QPointF;
#else
using DragPosition = QPoint;
#endif
namespace QtSpice {
struct SessionTestAccess {
    static FileTransferController &ready(Session &s) { s.state_ = Session::State::Connected; s.worker_->transfers.availability(true, {}); return s.worker_->transfers; }
};
}
class FileTransferTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void queueAndValidation() {
        QTemporaryDir dir; const auto path = dir.filePath("empty.txt"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        Session session; FileTransferController c(&session); QString error;
        QVERIFY(!c.send({path}, &error)); QVERIFY(!error.isEmpty());
        c.availability(true, {});
        QVERIFY(!c.send({path, dir.path()}, &error)); QVERIFY(c.rows.isEmpty());
        FileTransferController::Progress progress; FileTransferController::Completion done; int calls = 0;
        c.operation = [&](QString, auto p, auto d) { ++calls; progress = p; done = d; };
        QSignalSpy spy(&session, &Session::fileTransfersChanged);
        QVERIFY(c.send({path, path}, &error)); QCOMPARE(calls, 1); QCOMPARE(c.rows[1].state, S::Queued);
        QVERIFY(c.send({path}, &error)); QCOMPARE(calls, 1);
        progress(5000000000ULL, 6000000000ULL); QCOMPARE(c.rows[0].transferredBytes, 5000000000ULL);
        QCOMPARE(c.rows[0].state, S::Sending); done({}); QCOMPARE(c.rows[0].state, S::Completed);
        QTRY_COMPARE(calls, 2); done("Guest disk full"); QCOMPARE(c.rows[1].state, S::Failed);
        QTRY_COMPARE(calls, 3); done({}); QCOMPARE(c.rows[2].state, S::Completed);
        QVERIFY(spy.count() > 5); c.clearFinished(); QVERIFY(c.rows.isEmpty());
    }
    void cancellationAndReconnect() {
        QTemporaryDir dir; const auto path = dir.filePath("file"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        Session session; FileTransferController c(&session); c.availability(true, {});
        FileTransferController::Completion done; int calls = 0, cancels = 0;
        c.operation = [&](QString, auto, auto d) { ++calls; done = d; }; c.cancellation = [&] { ++cancels; };
        QVERIFY(c.send({path, path}, nullptr)); c.cancel(c.rows[0].id); QCOMPARE(c.rows[0].state, S::Cancelling); QCOMPARE(cancels, 1);
        done({}); QCOMPARE(c.rows[0].state, S::Cancelled); QTRY_COMPARE(calls, 2);
        auto late = done; c.detach(); QCOMPARE(c.rows[1].state, S::Cancelled); late({}); QCOMPARE(c.rows[1].state, S::Cancelled);
        c.availability(true, {}); QCoreApplication::processEvents(); QCOMPARE(calls, 2);
        c.operation = [&](QString, auto, auto d) { ++calls; done = d; }; c.cancellation = [&] { ++cancels; };
        QVERIFY(c.send({path, path}, nullptr)); c.availability(false, "Guest agent unavailable");
        QCOMPARE(c.rows.last().state, S::Cancelled); done("cancelled"); QCOMPARE(c.rows[2].state, S::Cancelled);
        QCOMPARE(c.rows[2].error, QString("Guest agent unavailable")); QCOMPARE(calls, 3);
    }
    void disappearingSourceAndLifetime() {
        QTemporaryDir dir; const auto a = dir.filePath("a"), b = dir.filePath("b");
        for (auto path : {a,b}) { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); }
        Session session; FileTransferController::Completion late;
        { FileTransferController c(&session); c.availability(true, {}); c.operation = [&](QString, auto, auto d) { late = d; };
          QVERIFY(c.send({a,b}, nullptr)); QVERIFY(QFile::remove(b)); late({});
          QTRY_COMPARE(c.rows[1].state, S::Failed);
          QVERIFY(c.send({a}, nullptr)); }
        late({});
    }
    void nativeUnavailable() {
        Session session; FileTransferController c(&session); auto *native = spice_session_new();
        auto *main = SPICE_MAIN_CHANNEL(spice_channel_new(native, SPICE_CHANNEL_MAIN, 0)); c.attach(main); c.availability(true, {});
        QTemporaryDir dir; const auto path = dir.filePath("empty"); QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.close();
        QVERIFY(c.send({path}, nullptr));
        QTRY_VERIFY_WITH_TIMEOUT(([&] { while (g_main_context_iteration(nullptr, false)) {} return c.rows[0].state == S::Failed; })(), 3000);
        QVERIFY(c.rows[0].error.contains(QStringLiteral("agent"), Qt::CaseInsensitive)); c.detach(); g_object_unref(native);
    }
    void dropAndPublicApi() {
        Session session; QString error;
        QVERIFY(!session.sendFiles({QStringLiteral("/missing")}, &error)); QVERIFY(!error.isEmpty());
        auto &c = SessionTestAccess::ready(session); FileTransferController::Completion done;
        c.operation = [&](QString, auto, auto d) { done = d; };
        QTemporaryDir dir; const auto path = dir.filePath("file"); QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.close();
        QMainWindow window; auto *widget = new Widget(&window); window.setCentralWidget(widget);
        FileSending sending(&window, &session); sending.add(&window); window.show();
        QVERIFY(window.findChild<QAction *>("sendFilesAction")->isEnabled());
        QMimeData remote; remote.setUrls({QUrl("https://example.invalid/file")});
        QDragEnterEvent rejected(DragPosition(1,1), Qt::CopyAction, &remote, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &rejected); QVERIFY(!rejected.isAccepted());
        QMimeData local; local.setUrls({QUrl::fromLocalFile(path)});
        QDragEnterEvent drag(DragPosition(1,1), Qt::CopyAction, &local, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &drag); QVERIFY(drag.isAccepted());
        QDropEvent drop(QPointF(1,1), Qt::CopyAction, &local, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &drop); QVERIFY(drop.isAccepted()); QCOMPARE(session.fileTransfers().size(), 1);
        QCOMPARE(session.fileTransfers()[0].filename, QString("file")); done({});
        session.clearFinishedFileTransfers(); QVERIFY(session.fileTransfers().isEmpty());
    }
    void sharedWindow() {
        Session session; QMainWindow first, second;
        first.setCentralWidget(new Widget(&first)); second.setCentralWidget(new Widget(&second));
        FileSending sending(&first, &session); sending.add(&first); sending.add(&second);
        auto open = [](QMainWindow &w) { for (auto *menu : w.menuBar()->findChildren<QMenu *>()) for (auto *action : menu->actions()) if (action->text() == "File Transfers…") { action->trigger(); return; } };
        open(first); QDialog *dialog = nullptr;
        for (auto *w : QApplication::topLevelWidgets()) if (w->objectName() == "fileTransfersDialog") dialog = qobject_cast<QDialog *>(w);
        QVERIFY(dialog); open(second); int count = 0;
        for (auto *w : QApplication::topLevelWidgets()) if (w->objectName() == "fileTransfersDialog") ++count;
        QCOMPARE(count, 1); QVERIFY(!first.findChild<QAction *>("sendFilesAction")->isEnabled());
        dialog->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); open(first);
    }
};
QTEST_MAIN(FileTransferTest)
#include "filetransfer_test.moc"
