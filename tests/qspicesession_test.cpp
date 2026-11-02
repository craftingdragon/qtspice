#include <QtSpice/QSpiceSession.h>
#include <QSignalSpy>
#include <QtTest>

class SessionTest : public QObject {
    Q_OBJECT
private slots:
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
        session.sendKey(0x1e, true);
        session.sendKey(0x1e, false);
        session.sendPointer(10, 20, 0);
        session.sendButton(1, true, 1);
        session.disconnectFromHost();
        session.sendButton(1, false, 0);
        QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
    }
};
QTEST_GUILESS_MAIN(SessionTest)
#include "qspicesession_test.moc"
