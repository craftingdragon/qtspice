#include "../src/core/qspicesession.h"
#include "../examples/usbdevices.h"
#include <QtTest>
#include <QSignalSpy>
using namespace QtSpice;
namespace QtSpice {
struct SessionTestAccess {
    static SpiceSession *native(Session &session) {
        auto *worker = session.worker_.get();
        worker->session = spice_session_new();
        worker->usb.attach(worker->session);
        g_signal_connect(worker->session, "channel-new", G_CALLBACK(Session::Worker::onChannelNew), worker);
        g_signal_connect(worker->session, "channel-destroy", G_CALLBACK(Session::Worker::onChannelDestroy), worker);
        return worker->session;
    }
    static void devices(Session &session, const QList<UsbDeviceInfo> &rows) { session.worker_->usb.update(rows, 1); }
};
}
class UsbTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void operations() {
        Session session; UsbController usb(&session);
        QSignalSpy changed(&session, &Session::usbChanged);
        usb.update({{1, "Device", false, UsbDeviceInfo::State::Available, true, {}}}, 1);
        UsbController::Completion done; int calls = 0;
        usb.operation = [&](quint64, bool, auto reply) { ++calls; done = reply; };
        usb.request(1, true); usb.request(1, true);
        QCOMPARE(calls, 1); QCOMPARE(usb.devices()[0].state, UsbDeviceInfo::State::Connecting);
        bool cancelled = false; usb.cancellation = [&] { cancelled = true; };
        usb.cancel(); QVERIFY(cancelled);
        done("Permission denied"); QCOMPARE(usb.devices()[0].state, UsbDeviceInfo::State::Available);
        QCOMPARE(usb.devices()[0].reason, QString("Permission denied"));
        usb.request(1, true); done({}); QCOMPARE(usb.devices()[0].state, UsbDeviceInfo::State::Connected);
        usb.request(1, false); done({}); QCOMPARE(usb.devices()[0].state, UsbDeviceInfo::State::Available);
        usb.request(1, true); auto late = done;
        usb.update({}, 1); late({}); QVERIFY(usb.devices().isEmpty());
        usb.update({{2, "Replugged", false, UsbDeviceInfo::State::Available, true, {}}}, 1);
        usb.request(2, true); late = done; usb.detach(); late({}); QVERIFY(usb.devices().isEmpty());
        QVERIFY(changed.count() >= 6);
    }
    void destructionAndUnavailable() {
        Session session; UsbController::Completion late;
        { UsbController usb(&session); usb.update({{5, "Unavailable", false, UsbDeviceInfo::State::Available, false, "No channels"}}, 0);
          usb.operation = [&](quint64, bool, auto done) { late = done; };
          usb.request(5, true); QVERIFY(!late);
          usb.update({{5, "Ready", false, UsbDeviceInfo::State::Available, true, {}}}, 1);
          usb.request(5, true); QVERIFY(late); }
        late({});
        QVERIFY(session.usbDevices().isEmpty()); QCOMPARE(session.usbFreeChannels(), 0);
    }
    void channelInventoryUpdates() {
        Session session;
        auto *native = SessionTestAccess::native(session);
        if (!session.usbError().isEmpty()) QSKIP("Native USB backend unavailable");
        QCOMPARE(session.usbFreeChannels(), 0);
        auto *first = spice_channel_new(native, SPICE_CHANNEL_USBREDIR, 0);
        auto *second = spice_channel_new(native, SPICE_CHANNEL_USBREDIR, 1);
        // Deliver the Qt refresh after the manager's native AFTER handlers.
        QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
        QCOMPARE(session.usbFreeChannels(), 2);
        g_signal_emit_by_name(native, "channel-destroy", first);
        QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
        QCOMPARE(session.usbFreeChannels(), 1);
        g_signal_emit_by_name(native, "channel-destroy", second);
        QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
        QCOMPARE(session.usbFreeChannels(), 0);
        // A late refresh from the previous session must not repopulate state.
        spice_channel_new(native, SPICE_CHANNEL_USBREDIR, 2);
        session.disconnectFromHost();
        QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
        QCOMPARE(session.usbFreeChannels(), 0);
    }
    void nativeLifecycle() {
        Session owner; UsbController usb(&owner);
        for (int i = 0; i < 3; ++i) {
            auto *native = spice_session_new(); usb.attach(native);
            QVERIFY(usb.freeChannels >= 0);
            usb.detach(); g_object_unref(native);
            QCoreApplication::processEvents();
            QVERIFY(usb.devices().isEmpty()); QCOMPARE(usb.freeChannels, 0);
        }
    }
    void sharedDialog() {
        Session session; QMainWindow first, second;
        UsbDevices::add(&first, &session); UsbDevices::add(&second, &session);
        first.findChild<QAction *>("usbDevicesAction")->trigger();
        auto *dialog = qobject_cast<QDialog *>(session.property("usbDialog").value<QObject *>()); QVERIFY(dialog);
        second.findChild<QAction *>("usbDevicesAction")->trigger();
        QCOMPARE(session.property("usbDialog").value<QObject *>(), dialog);
        auto *showInput = dialog->findChild<QCheckBox *>();
        QVERIFY(!showInput->isChecked());
        SessionTestAccess::devices(session, {
            {1, "Same name", false, UsbDeviceInfo::State::Available, true, {}},
            {2, "Same name", true, UsbDeviceInfo::State::Available, true, {}},
            {3, "Keyboard", true, UsbDeviceInfo::State::Connected, false, {}}
        });
        auto *tree = dialog->findChild<QTreeWidget *>();
        QCOMPARE(tree->topLevelItemCount(), 2);
        showInput->setChecked(true); QCOMPARE(tree->topLevelItemCount(), 3);
        showInput->setChecked(false); QCOMPARE(tree->topLevelItemCount(), 2);
        SessionTestAccess::devices(session, {}); QCOMPARE(tree->topLevelItemCount(), 0);
        QVERIFY(dialog->findChild<QLabel *>()->text().contains("unavailable"));
        delete dialog; QVERIFY(!session.property("usbDialog").isValid());
    }
};
QTEST_MAIN(UsbTest)
#include "usb_test.moc"
