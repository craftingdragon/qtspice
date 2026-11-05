#include <QtSpice/QSpiceSession.h>
#include "../src/core/clipboardcapability.h"
#include <QSignalSpy>
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
