// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtSpice/QSpiceConnection.h>
#include <QtSpice/QSpiceDisplay.h>
#include <QtSpice/QSpiceSession.h>
#include <QtSpice/QSpiceWidget.h>
#include <QtTest>

class PublicApiTest : public QObject {
    Q_OBJECT
signals:
    void qtKeywordsEnabled();
private slots:
    void attachment() {
        QtSpice::Widget widget;
        auto *session = new QtSpice::Session;
        QSignalSpy changes(&widget, &QtSpice::Widget::sessionChanged);
        widget.setSession(session);
        QCOMPARE(widget.session(), session);
        widget.setSession(session);
        QCOMPARE(changes.count(), 1);
        delete session;
        QVERIFY(!widget.session());
        QCOMPARE(changes.count(), 2);
        emit qtKeywordsEnabled();
    }
};
QTEST_MAIN(PublicApiTest)
#include "publicapi_test.moc"
