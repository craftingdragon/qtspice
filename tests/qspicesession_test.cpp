#include <QtSpice/QSpiceSession.h>
#include "../src/core/clipboardcapability.h"
#include <QSignalSpy>
#include <QtTest>

class SessionTest : public QObject {
    Q_OBJECT
private slots:
    void clipboardRequiresByDemandCapability() {
        QVERIFY(!QtSpice::clipboardByDemandReady(false, false));
        QVERIFY(!QtSpice::clipboardByDemandReady(true, false));
        QVERIFY(!QtSpice::clipboardByDemandReady(false, true));
        QVERIFY(QtSpice::clipboardByDemandReady(true, true));
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
