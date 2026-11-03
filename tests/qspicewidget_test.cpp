#include <QtSpice/QSpiceWidget.h>
#include <QtSpice/QSpiceSession.h>
#include <QSignalSpy>
#include <QtTest>

class WidgetTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void debouncesAndDeduplicates() {
        QtSpice::Session session;
        QtSpice::Widget widget;
        widget.setSession(&session);
        QSignalSpy requests(&widget, &QtSpice::Widget::guestResizeRequested);
        widget.resize(800, 600);
        widget.show();
        widget.resize(900, 600);
        widget.resize(1000, 700);
        QTRY_COMPARE_WITH_TIMEOUT(requests.size(), 1, 1000);
        QCOMPARE(requests.takeFirst().at(0).toSize(), QSize(1000, 700));
        widget.resize(1001, 700);
        widget.resize(1000, 700);
        QTest::qWait(350);
        QCOMPARE(requests.size(), 0);
    }
    void toggleCancelsPendingAndResumes() {
        QtSpice::Session session;
        QtSpice::Widget widget;
        widget.setSession(&session);
        QSignalSpy requests(&widget, &QtSpice::Widget::guestResizeRequested);
        widget.resize(800, 600);
        widget.show();
        widget.setAutoResizeGuest(false);
        QTest::qWait(350);
        QCOMPARE(requests.size(), 0);
        widget.setAutoResizeGuest(true);
        QTRY_COMPARE_WITH_TIMEOUT(requests.size(), 1, 1000);
        QCOMPARE(requests.takeFirst().at(0).toSize(), QSize(800, 600));
        widget.hide();
        widget.resize(900, 700);
        QTest::qWait(350);
        QCOMPARE(requests.size(), 0);
        widget.show();
        QTRY_COMPARE_WITH_TIMEOUT(requests.size(), 1, 1000);
        QCOMPARE(requests.takeFirst().at(0).toSize(), QSize(900, 700));
    }
};
QTEST_MAIN(WidgetTest)
#include "qspicewidget_test.moc"
