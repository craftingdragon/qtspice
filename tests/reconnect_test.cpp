#include "../examples/reconnect.h"
#include <QtTest>
class ReconnectTest : public QObject {
    Q_OBJECT
private slots:
    void cancellationIgnoresLateCredentials() {
        QMainWindow window; window.setCentralWidget(new QWidget); QtSpice::Session session;
        Reconnect retry(&window, &session); Reconnect::Result pending; int calls = 0;
        retry.source = [&](auto done) { ++calls; pending = done; };
        retry.start(); retry.start(); QCOMPARE(calls, 1);
        retry.cancel(); QtSpice::ConnectionConfig config; config.host = "127.0.0.1"; config.port = 1;
        pending(config, {}); QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
        retry.start(); QCOMPARE(calls, 2); pending({}, "No ticket");
        QVERIFY(window.findChild<QLabel *>()->text().contains("No ticket"));
        retry.start(); QCOMPARE(calls, 3);
    }
    void lateReplyAfterDestructionIsSafe() {
        QtSpice::Session session; Reconnect::Result pending;
        { QMainWindow window; window.setCentralWidget(new QWidget);
          auto *retry = new Reconnect(&window, &session); retry->source = [&](auto done) { pending = done; }; retry->start(); }
        pending({}, "expired"); QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
    }
    void everyWindowSharesOneAttempt() {
        QMainWindow primary, extra; primary.setCentralWidget(new QWidget); extra.setCentralWidget(new QWidget);
        QtSpice::Session session; Reconnect retry(&primary, &session); retry.attach(&extra);
        int count = 0; retry.source = [&](auto) { ++count; };
        extra.findChild<QPushButton *>()->click(); primary.findChild<QPushButton *>()->click(); QCOMPARE(count, 1);
        retry.cancel(); QVERIFY(extra.findChild<QPushButton *>()->isEnabled());
    }
};
QTEST_MAIN(ReconnectTest)
#include "reconnect_test.moc"
