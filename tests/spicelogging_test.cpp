#include "../src/core/spicelogging_p.h"
#include <QtSpice/QSpiceSession.h>
#include <QtTest>
#include <QProcess>

class SpiceLoggingTest : public QObject {
    Q_OBJECT
private slots:
    void otherDiagnosticsRemainVisible() {
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--probe-forward")});
        QVERIFY(child.waitForFinished(5000));
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
        const QByteArray stderrOutput = child.readAllStandardError();
        QVERIFY(stderrOutput.contains("unrelated SPICE warning"));
        QVERIFY(stderrOutput.contains("cursor_handle_init: assertion"));
        QVERIFY(stderrOutput.contains("unrelated domain warning"));
    }
    void exactWarningOnly() {
        using SpiceLogging::isBackpressureWarning;
        QVERIFY(isBackpressureWarning("GSpice", G_LOG_LEVEL_WARNING, g_strerror(EAGAIN)));
        QVERIFY(isBackpressureWarning("GSpice", G_LOG_LEVEL_WARNING, g_strerror(EWOULDBLOCK)));
        QVERIFY(!isBackpressureWarning("GSpice", G_LOG_LEVEL_CRITICAL, g_strerror(EAGAIN)));
        QVERIFY(!isBackpressureWarning("GSpice", GLogLevelFlags(G_LOG_LEVEL_WARNING | G_LOG_FLAG_FATAL), g_strerror(EAGAIN)));
        QVERIFY(!isBackpressureWarning("GSpice", GLogLevelFlags(G_LOG_LEVEL_WARNING | G_LOG_FLAG_RECURSION), g_strerror(EAGAIN)));
        QVERIFY(!isBackpressureWarning("Other", G_LOG_LEVEL_WARNING, g_strerror(EAGAIN)));
        QVERIFY(!isBackpressureWarning(nullptr, G_LOG_LEVEL_WARNING, g_strerror(EAGAIN)));
        QVERIFY(!isBackpressureWarning("GSpice", G_LOG_LEVEL_WARNING, nullptr));
        QVERIFY(!isBackpressureWarning("GSpice", G_LOG_LEVEL_WARNING, "Connection failed"));
        const QByteArray extended = QByteArray("Connection failed: ") + g_strerror(EAGAIN);
        QVERIFY(!isBackpressureWarning("GSpice", G_LOG_LEVEL_WARNING, extended.constData()));
        QVERIFY(!isBackpressureWarning("GSpice", G_LOG_LEVEL_CRITICAL,
            "cursor_handle_init: assertion 'c->init_done == FALSE' failed"));
    }
    void defaultQuietAndDebugOverride() {
        QtSpice::Session first, second; // Native logging is initialized internally, once.
        QLoggingCategory::setFilterRules(QStringLiteral("qtspice.transport.debug=false"));
        QVERIFY(!SpiceLogging::transportCategory().isDebugEnabled());
        g_log("GSpice", G_LOG_LEVEL_WARNING, "%s", g_strerror(EAGAIN));
        QLoggingCategory::setFilterRules(QStringLiteral("qtspice.transport.debug=true"));
        QVERIFY(SpiceLogging::transportCategory().isDebugEnabled());
        QTest::ignoreMessage(QtDebugMsg, (QByteArray("SPICE TLS write waiting for socket buffer space: ") + g_strerror(EAGAIN)).constData());
        g_log("GSpice", G_LOG_LEVEL_WARNING, "%s", g_strerror(EAGAIN));
        QLoggingCategory::setFilterRules({});
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--probe-forward"))) {
        QtSpice::Session session;
        g_log("GSpice", G_LOG_LEVEL_WARNING, "unrelated SPICE warning");
        g_log("GSpice", G_LOG_LEVEL_CRITICAL, "cursor_handle_init: assertion 'c->init_done == FALSE' failed");
        g_log("Other", G_LOG_LEVEL_WARNING, "unrelated domain warning");
        return 0;
    }
    SpiceLoggingTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "spicelogging_test.moc"
