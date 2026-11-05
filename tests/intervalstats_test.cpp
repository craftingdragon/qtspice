#include "../src/core/intervalstats.h"
#include <QScopeGuard>
#include <QtTest>

namespace {
QStringList messages;
void capture(QtMsgType, const QMessageLogContext &context, const QString &message) {
    if (QString::fromLatin1(context.category) == QStringLiteral("qtspice.performance")) messages.append(message);
}
bool hasValue(const QString &line, const QString &key, quint64 value) {
    return line.contains(QRegularExpression(QStringLiteral("\"%1\"[^0-9]*%2\\b").arg(key).arg(value)));
}
}

class IntervalStatsTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void samples() {
        if (!QCoreApplication::arguments().contains(QStringLiteral("--performance-stats"))) {
            QLoggingCategory::setFilterRules(QStringLiteral("qtspice.performance.debug=true"));
            QtSpice::IntervalStats stats("disabled", this);
            QVERIFY(!stats.enabled());
            int refreshes = 0;
            stats.updateGauges = [&] { ++refreshes; };
            stats.sample();
            QTest::qWait(1100);
            QCOMPARE(refreshes, 0);
            QLoggingCategory::setFilterRules({});
            return;
        }
        messages.clear();
        const auto previous = qInstallMessageHandler(capture);
        const auto restore = qScopeGuard([previous] {
            qInstallMessageHandler(previous);
            QLoggingCategory::setFilterRules({});
        });
        QLoggingCategory::setFilterRules(QStringLiteral("qtspice.performance.debug=false"));
        QtSpice::IntervalStats stats("test", this);
        stats.add(QStringLiteral("bytes"), 100);
        stats.sample();
        QVERIFY(messages.isEmpty());
        QLoggingCategory::setFilterRules(QStringLiteral("qtspice.performance.debug=true"));
        for (int i = 0; i < 500; ++i) stats.add(QStringLiteral("bytes"), 2);
        stats.gauge(QStringLiteral("pending"), 42);
        QVERIFY(messages.isEmpty()); // Counters never log per event.
        stats.sample();
        QCOMPARE(messages.size(), 1);
        QVERIFY2(hasValue(messages.last(), QStringLiteral("bytes"), 1000), qPrintable(messages.last()));
        QVERIFY(hasValue(messages.last(), QStringLiteral("pending"), 42));
        stats.sample();
        QCOMPARE(messages.size(), 2);
        QVERIFY(hasValue(messages.last(), QStringLiteral("bytes"), 0));
        QVERIFY(hasValue(messages.last(), QStringLiteral("pending"), 42));
        stats.updateGauges = [&stats] { stats.gauge(QStringLiteral("pending"), 7); };
        stats.add(QStringLiteral("bytes"), 12);
        const int before = messages.size();
        QTest::qWait(100);
        QCOMPARE(messages.size(), before);
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), before + 1, 1800);
        QVERIFY(hasValue(messages.last(), QStringLiteral("bytes"), 12));
        QVERIFY(hasValue(messages.last(), QStringLiteral("pending"), 7));
        QVERIFY(messages.last().contains(QStringLiteral("interval-ms")));
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    IntervalStatsTest test;
    return QTest::qExec(&test, QStringList{QString::fromLocal8Bit(argv[0])});
}
#include "intervalstats_test.moc"
