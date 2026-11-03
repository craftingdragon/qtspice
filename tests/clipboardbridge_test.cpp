#include "clipboardbridge.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QtTest>

class ClipboardBridgeTest : public QObject {
    Q_OBJECT
private slots:
    void hostAndGuestChanges() {
        QtSpice::ClipboardBridge bridge;
        QSignalSpy host(&bridge, &QtSpice::ClipboardBridge::hostTextChanged);
        QGuiApplication::clipboard()->setText(QStringLiteral("before"));
        bridge.setActive(true);
        QCOMPARE(host.size(), 1);
        QCOMPARE(host.takeFirst().at(0).toString(), QStringLiteral("before"));
        QGuiApplication::clipboard()->setText(QStringLiteral("høst → guest"));
        QCOMPARE(host.size(), 1);
        QCOMPARE(host.takeFirst().at(0).toString(), QStringLiteral("høst → guest"));
        bridge.applyGuestText(QStringLiteral("guest → host"));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("guest → host"));
        QCOMPARE(host.size(), 0);
        bridge.setActive(false);
        QGuiApplication::clipboard()->setText(QStringLiteral("after"));
        QCOMPARE(host.size(), 0);
        bridge.applyGuestText(QStringLiteral("ignored"));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("after"));
        bridge.setActive(true);
        QCOMPARE(host.size(), 1);
        QCOMPARE(host.takeFirst().at(0).toString(), QStringLiteral("after"));
        bridge.applyGuestText(QString());
        QCOMPARE(QGuiApplication::clipboard()->text(), QString());
        QCOMPARE(host.size(), 0);
    }
};
QTEST_MAIN(ClipboardBridgeTest)
#include "clipboardbridge_test.moc"
