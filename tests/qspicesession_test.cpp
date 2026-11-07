#include <QtSpice/QSpiceSession.h>
#include "../src/core/clipboardcapability.h"
#include <QSignalSpy>
#undef signals
#include "../src/core/qspicesession.h"
namespace QtSpice { struct SessionTestAccess { static auto &worker(Session &s) { return *s.worker_; } static void observed(Session &s, const QList<MonitorInfo> &layout, bool pending) { s.worker_->layoutPending = pending; s.worker_->observeMonitorLayout(layout); } static void actual(Session &s, const QList<MonitorInfo> &layout) { s.monitors_ = layout; } static bool pending(Session &s) { return s.worker_->layoutPending; } static auto &desired(Session &s) { return s.worker_->desiredMonitors; } }; }
#include <QtTest>
#include <QLoggingCategory>
#include <QScopeGuard>

namespace {
QStringList connectionMessages;
void captureConnectionLog(QtMsgType, const QMessageLogContext &context, const QString &message) {
    if (QString::fromLatin1(context.category) == QStringLiteral("qtspice.connection"))
        connectionMessages.append(message);
}
}

class SessionTest : public QObject {
    Q_OBJECT
private slots:
    void batchedDisplayAndStaleWork() {
        using namespace QtSpice;
        Session s;
        auto &worker = SessionTestAccess::worker(s);
        SpiceSession *native = spice_session_new();
        auto *channel = spice_channel_new(native, SPICE_CHANNEL_DISPLAY, 7);
        auto cleanup = qScopeGuard([&] { worker.displays.clear(); g_object_unref(native); });
        worker.displays[7] = SPICE_DISPLAY_CHANNEL(channel);
        QImage pixels(100,80,QImage::Format_RGB32); pixels.fill(Qt::red);
        worker.primaryReader = [&](SpiceChannel *, SpiceDisplayPrimary *p) {
            p->format = SPICE_SURFACE_FMT_32_xRGB; p->width = pixels.width(); p->height = pixels.height();
            p->stride = pixels.bytesPerLine(); p->data = pixels.bits(); return true;
        };
        SessionTestAccess::actual(s, {{{7,0},{0,0,50,80},true},{{7,1},{50,0,50,80},true}});
        QSignalSpy updates(&s, &Session::monitorFrameUpdated);
        worker.scheduleDisplay(channel);
        QCoreApplication::sendPostedEvents();
        QCOMPARE(updates.size(),2); updates.clear();
        auto retained = s.monitorFrame({7,0});
        pixels.setPixelColor(3,4,Qt::blue); pixels.setPixelColor(51,4,Qt::green);
        worker.scheduleDisplay(channel,{3,4,1,1}); worker.scheduleDisplay(channel,{51,4,1,1});
        QCOMPARE(updates.size(),0);
        QCoreApplication::sendPostedEvents();
        QCOMPARE(updates.size(),2);
        QCOMPARE(s.monitorFrame({7,0}).pixelColor(3,4),QColor(Qt::blue));
        QCOMPARE(s.monitorFrame({7,1}).pixelColor(1,4),QColor(Qt::green));
        QCOMPARE(retained.pixelColor(3,4),QColor(Qt::red));
        updates.clear();
        worker.scheduleDisplay(channel,{0,0,1,1}); worker.clearDisplay(7);
        QVERIFY(s.monitorFrame({7,0}).isNull()); updates.clear();
        QCoreApplication::sendPostedEvents(); QVERIFY(updates.isEmpty());
        int nonemptyLegacy = 0;
        connect(&s, &Session::monitorFrameReady, &s, [&](MonitorId, const QImage &image) { if (!image.isNull()) ++nonemptyLegacy; });
        bool cleared = false;
        connect(&s, &Session::monitorFrameUpdated, &s, [&](MonitorId, const QRegion &) {
            if (!cleared) { cleared = true; worker.clearDisplay(7); }
        });
        worker.scheduleDisplay(channel); QCoreApplication::sendPostedEvents();
        QCOMPARE(nonemptyLegacy,0);
        worker.scheduleDisplay(channel); s.disconnectFromHost(); updates.clear();
        QCoreApplication::sendPostedEvents(); QVERIFY(updates.isEmpty());
    }

    void alreadyAppliedLayoutNeedsNoNewGuestEvent() {
        QtSpice::Session s;
        const QList<QtSpice::MonitorInfo> actual{{{0,0},{0,0,800,600},true},{{0,1},{800,0,1024,768},true}};
        QtSpice::SessionTestAccess::actual(s, actual);
        QSignalSpy status(&s, &QtSpice::Session::monitorConfigurationStatusChanged);
        s.requestMonitorLayout(actual);
        QVERIFY(!QtSpice::SessionTestAccess::pending(s));
        QVERIFY(!status.isEmpty()); QVERIFY(status.last().first().toString().isEmpty());
        s.setDesiredMonitorSize({0,1}, QSize(1280,720));
        QVERIFY(QtSpice::SessionTestAccess::pending(s));
        s.requestMonitorLayout(actual); // Restore the already-applied layout while another request is pending.
        QVERIFY(!QtSpice::SessionTestAccess::pending(s));
    }
    void lateGuestTopologyRetiresTimedOutWarning() {
        QtSpice::Session s;
        const QList<QtSpice::MonitorInfo> requested{{{0,0}, {0,0,800,600}, true}, {{0,1}, {800,0,1024,768}, true}};
        s.requestMonitorLayout(requested);
        QSignalSpy status(&s, &QtSpice::Session::monitorConfigurationStatusChanged);
        const QList<QtSpice::MonitorInfo> actual{{{0,0}, {0,0,800,600}, true}, {{0,1}, {800,0,640,480}, true}};
        QtSpice::SessionTestAccess::observed(s, actual, true);
        QVERIFY(status.isEmpty()); // Still pending: a different size is not confirmation.
        QtSpice::SessionTestAccess::observed(s, actual, false); // New topology after timeout/manual activation.
        QVERIFY(!status.isEmpty()); QVERIFY(status.last().first().toString().isEmpty());
        QCOMPARE(QtSpice::SessionTestAccess::desired(s).value({0,1}).geometry.size(), QSize(640,480));
        s.requestMonitorLayout(requested);
        status.clear();
        QtSpice::SessionTestAccess::observed(s, requested, true);
        QVERIFY(!status.isEmpty()); QVERIFY(status.last().first().toString().isEmpty());
    }
    void connectionDiagnostics() {
        connectionMessages.clear();
        const auto previousHandler = qInstallMessageHandler(captureConnectionLog);
        const auto restore = qScopeGuard([previousHandler] {
            qInstallMessageHandler(previousHandler);
            QLoggingCategory::setFilterRules({});
        });
        QLoggingCategory::setFilterRules(QStringLiteral("qtspice.connection.debug=false"));
        {
            QtSpice::Session session;
            session.connectToHost(QStringLiteral("127.0.0.1"), 1);
            session.disconnectFromHost();
        }
        QVERIFY(connectionMessages.isEmpty());
        QLoggingCategory::setFilterRules(QStringLiteral("qtspice.connection.debug=true"));
        {
            QtSpice::Session session;
            QSignalSpy errors(&session, &QtSpice::Session::errorOccurred);
            QtSpice::ConnectionConfig config;
            config.host = QStringLiteral("127.0.0.1");
            config.port = 1;
            config.password = QStringLiteral("diagnostic-test-secret");
            session.connectToConfig(config);
            QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 5000);
        }
        const QString messages = connectionMessages.join(QLatin1Char('\n'));
        QVERIFY(messages.contains(QStringLiteral("connection requested")));
        QVERIFY(messages.contains(QStringLiteral("channel created main id 0")));
        QVERIFY(messages.contains(QStringLiteral("error-connect")));
        QVERIFY(messages.contains(QStringLiteral("error-domain")));
        QVERIFY(messages.contains(QStringLiteral("local teardown requested")));
        QVERIFY(!messages.contains(QStringLiteral("diagnostic-test-secret")));
    }
    void clipboardRequiresByDemandCapability() {
        QVERIFY(!QtSpice::clipboardByDemandReady(false, false));
        QVERIFY(!QtSpice::clipboardByDemandReady(true, false));
        QVERIFY(!QtSpice::clipboardByDemandReady(false, true));
        QVERIFY(QtSpice::clipboardByDemandReady(true, true));
    }
    void monitorLayoutPreservesOtherHeads() {
        QtSpice::Session s;
        s.requestMonitorLayout({{{0,0}, {0,0,800,600}, true}, {{0,1}, {800,0,640,480}, true}});
        s.setDesiredMonitorSize({0,1}, {1280,720});
        auto &desired = QtSpice::SessionTestAccess::desired(s);
        QCOMPARE(desired.size(), 2);
        QCOMPARE(desired.value({0,0}).geometry.size(), QSize(800,600));
        QCOMPARE(desired.value({0,1}).geometry, QRect(800,0,1280,720));
        s.setMonitorEnabled({0,1}, false);
        QVERIFY(!desired.value({0,1}).enabled);
        s.setMonitorEnabled({0,1}, true);
        QCOMPARE(desired.value({0,1}).geometry.size(), QSize(1280,720));
        s.setMonitorEnabled({}, false);
        QVERIFY(desired.value({}).enabled);
        const auto before = desired;
        s.requestMonitorLayout({{{0,1},{0,0,800,600},true}, {{1,0},{800,0,800,600},true}});
        QCOMPARE(desired, before); // Colliding wire IDs must not send a partial layout.
        s.requestMonitorLayout({{{0,0},{0,0,20000,600},true}});
        QCOMPARE(desired, before);
    }
    void cancelledAttemptsCannotPublishLateFailure() {
        QtSpice::Session session;
        QSignalSpy errors(&session, &QtSpice::Session::errorOccurred);
        for (int i = 0; i < 10; ++i) {
            session.connectToHost(QStringLiteral("127.0.0.1"), 1);
            session.disconnectFromHost();
            QCoreApplication::processEvents();
            QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
        }
        QTest::qWait(30);
        QVERIFY(errors.isEmpty());
        QVERIFY(!session.inputReady()); QVERIFY(!session.clipboardReady());
        QVERIFY(session.monitors().isEmpty());
    }
    void initialState() {
        QtSpice::Session session;
        QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
    }
    void rejectInvalidAddress() {
        QtSpice::Session session;
        QSignalSpy errors(&session, &QtSpice::Session::errorOccurred);
        session.connectToHost({}, 0);
        QCOMPARE(errors.size(), 1);
        QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
    }
    void disconnectIsIdempotent() {
        QtSpice::Session session;
        QSignalSpy states(&session, &QtSpice::Session::stateChanged);
        session.disconnectFromHost();
        session.disconnectFromHost();
        QCOMPARE(states.size(), 0);
    }
    void rejectInvalidConfig() {
        QtSpice::Session session;
        QSignalSpy errors(&session, &QtSpice::Session::errorOccurred);
        QtSpice::ConnectionConfig config;
        config.host = QStringLiteral("example.invalid");
        session.connectToConfig(config);
        QCOMPARE(errors.size(), 1);
        QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
    }
    void failedConnectionReportsError() {
        QtSpice::Session session;
        QSignalSpy errors(&session, &QtSpice::Session::errorOccurred);
        session.connectToHost(QStringLiteral("127.0.0.1"), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 5000);
        QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
    }
    void invalidProxyIsRejectedBeforeConnecting() {
        QtSpice::Session session;
        QSignalSpy errors(&session, &QtSpice::Session::errorOccurred);
        QtSpice::ConnectionConfig config;
        config.host = QStringLiteral("guest.invalid"); config.port = 5900;
        config.networkProxy = QStringLiteral("ftp://user:secret@proxy.invalid");
        session.connectToConfig(config);
        QCOMPARE(errors.size(), 1);
        QVERIFY(!errors.first().first().toString().contains(QStringLiteral("secret")));
        QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
    }
    void disconnectedInputIsIgnored() {
        QtSpice::Session session;
        QCOMPARE(session.mouseMode(), QtSpice::Session::MouseMode::Client);
        QVERIFY(!session.inputReady());
        QVERIFY(!session.clipboardReady());
        session.setHostClipboardText(QStringLiteral("test"));
        session.setHostClipboardText(QString(4 * 1024 * 1024 + 1, QLatin1Char('x')));
        session.sendKey(0x1e, true);
        session.sendKey(0x1e, false);
        session.sendPointer(10, 20, 0);
        session.sendRelativePointer(1, -1, 0);
        session.sendButton(1, true, 1);
        session.releaseAllInputs();
        session.disconnectFromHost();
        session.sendButton(1, false, 0);
        QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
    }
    void destroyDuringHandshake() {
        for (int i = 0; i < 20; ++i) {
            auto *session = new QtSpice::Session;
            session->connectToHost(QStringLiteral("127.0.0.1"), 1);
            QCoreApplication::processEvents();
            delete session;
            QCoreApplication::processEvents();
        }
    }
};
QTEST_GUILESS_MAIN(SessionTest)
#include "qspicesession_test.moc"
