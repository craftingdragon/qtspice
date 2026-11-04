#include "../examples/sharedfolder.h"
#include "../src/core/qspicesession.h"
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QSignalSpy>
#include <QtTest>

namespace QtSpice {
struct SessionTestAccess {
    static void createBackend(Session &s) { s.worker_->session = spice_session_new(); s.worker_->applySharing(); }
    static SpiceChannel *addChannel(Session &s) {
        auto *channel = spice_channel_new(s.worker_->session, SPICE_CHANNEL_WEBDAV, 0);
        // Initialize the native cancellation state without a VM connection.
        g_signal_emit_by_name(channel, "port-event", SPICE_PORT_EVENT_OPENED);
        Session::Worker::onChannelNew(s.worker_->session, channel, s.worker_.get());
        return channel;
    }
    static void configureWithoutConnecting(Session &s, const SharedDirectory &share) {
        s.sharedDirectory_ = share;
        s.worker_->applySharing();
    }
    static SpiceSession *backend(Session &s) { return s.worker_->session; }
};
}
class SharedFolderTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void validationAndIsolation() {
        QTemporaryDir dir;
        QTemporaryFile file; QVERIFY(file.open());
        QtSpice::Session a, b;
        QString error;
        QVERIFY(!a.setSharedDirectory({file.fileName(), QtSpice::SharedDirectoryAccess::ReadOnly}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!a.setSharedDirectory({{}, QtSpice::SharedDirectoryAccess::ReadOnly}));
        QVERIFY(!a.setSharedDirectory({dir.path(), static_cast<QtSpice::SharedDirectoryAccess>(77)}));
        QVERIFY(a.setSharedDirectory({dir.path(), QtSpice::SharedDirectoryAccess::ReadOnly}));
        QCOMPARE(a.sharingState(), QtSpice::Session::SharingState::WaitingForChannel);
        QVERIFY(!b.sharedDirectory());
        QVERIFY(!a.setSharedDirectory({file.fileName(), QtSpice::SharedDirectoryAccess::ReadWrite}));
        QCOMPARE(a.sharedDirectory()->access, QtSpice::SharedDirectoryAccess::ReadOnly);
        a.disconnectFromHost();
        QVERIFY(!a.sharedDirectory());
        QCOMPARE(a.sharingState(), QtSpice::Session::SharingState::Disabled);
    }
    void backendProperties() {
        QTemporaryDir first, second;
        QtSpice::Session session;
        QtSpice::SessionTestAccess::createBackend(session);
        auto *backend = QtSpice::SessionTestAccess::backend(session);
        auto check = [&](const QString &expected, bool readOnly) {
            gchar *path = nullptr; gboolean ro = FALSE;
            g_object_get(backend, "shared-dir", &path, "share-dir-ro", &ro, nullptr);
            QCOMPARE(QString::fromUtf8(path), expected);
            QCOMPARE(bool(ro), readOnly);
            g_free(path);
        };
        check({}, true);
        QVERIFY(session.setSharedDirectory({first.path(), QtSpice::SharedDirectoryAccess::ReadOnly}));
        check(first.path(), true);
        QVERIFY(session.setSharedDirectory({second.path(), QtSpice::SharedDirectoryAccess::ReadWrite}));
        check(second.path(), false);
        session.disableSharedDirectory(); check({}, true);
        QVERIFY(session.setSharedDirectory({first.path(), QtSpice::SharedDirectoryAccess::ReadOnly}));
        session.disconnectFromHost();
        QTest::qWait(150); // Pending reconnect callbacks must be harmless after teardown.
        QVERIFY(!session.sharedDirectory());
    }
    void channelLifecycle() {
        QTemporaryDir dir;
        QtSpice::Session session;
        QtSpice::SessionTestAccess::createBackend(session);
        auto *channel = QtSpice::SessionTestAccess::addChannel(session);
        QCOMPARE(session.sharingState(), QtSpice::Session::SharingState::Disabled);
        QtSpice::SessionTestAccess::configureWithoutConnecting(session,
            {dir.path(), QtSpice::SharedDirectoryAccess::ReadOnly});
        QCOMPARE(session.sharingState(), QtSpice::Session::SharingState::Connecting);
        g_signal_emit_by_name(channel, "channel-event", SPICE_CHANNEL_OPENED);
        QCOMPARE(session.sharingState(), QtSpice::Session::SharingState::WaitingForGuest);
        g_signal_emit_by_name(channel, "channel-event", SPICE_CHANNEL_ERROR_CONNECT);
        QCOMPARE(session.sharingState(), QtSpice::Session::SharingState::Error);
        QVERIFY(!session.sharingError().isEmpty());
        session.disableSharedDirectory();
        QCOMPARE(session.sharingState(), QtSpice::Session::SharingState::Disabled);
        session.disconnectFromHost();
    }
    void commandLine() {
        QTemporaryDir dir;
        const QList<QStringList> invalid = {
            {"viewer", "--shared-dir", dir.path()}, {"viewer", "--shared-dir-mode", "ro"},
            {"viewer", "--shared-dir", dir.path(), "--shared-dir-mode", "bad"}};
        for (const auto &args : invalid) {
            QCommandLineParser p; SharedFolder::addOptions(p); QVERIFY(p.parse(args));
            std::optional<QtSpice::SharedDirectory> share; QString error;
            QVERIFY(!SharedFolder::parse(p, share, error)); QVERIFY(!error.isEmpty());
        }
        for (const auto &mode : {QStringLiteral("ro"), QStringLiteral("rw")}) {
            QCommandLineParser p; SharedFolder::addOptions(p);
            QVERIFY(p.parse({"viewer", "--shared-dir", dir.path(), "--shared-dir-mode", mode}));
            std::optional<QtSpice::SharedDirectory> share; QString error;
            QVERIFY(SharedFolder::parse(p, share, error)); QVERIFY(share);
            QCOMPARE(share->access, mode == "ro" ? QtSpice::SharedDirectoryAccess::ReadOnly : QtSpice::SharedDirectoryAccess::ReadWrite);
        }
    }
    void dialogCancellation() {
        QtSpice::Session session;
        QMenuBar bar;
        SharedFolder::addMenu(&bar, &session);
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            for (auto *button : dialog->findChildren<QRadioButton *>()) QVERIFY(!button->isChecked());
            dialog->findChild<QLineEdit *>()->setText(QStringLiteral("/tmp"));
            dialog->reject();
        });
        bar.actions().first()->menu()->actions().first()->trigger();
        QVERIFY(!session.sharedDirectory());
    }
};
QTEST_MAIN(SharedFolderTest)
#include "sharedfolder_test.moc"
