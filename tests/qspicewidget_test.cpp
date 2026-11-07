#include <QtSpice/QSpiceWidget.h>
#include <QtSpice/QSpiceSession.h>
#include <QSignalSpy>
#include <QtTest>
#include <QScrollBar>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTemporaryDir>
#include <QSettings>
#include "../examples/scalingmenu.h"
#include "../examples/monitorwindows.h"
#include "../examples/inputmenu.h"
#include "../src/widgets/displaygeometry.h"

namespace QtSpice {
struct SessionTestAccess {
    static void frame(Session &s, QImage image, MonitorId id = {}, QRegion damage = {}) { s.frames_[id] = image; Q_EMIT s.monitorFrameUpdated(id, damage.isEmpty() ? QRegion(image.rect()) : damage); }
    static void available(Session &s) { s.monitorConfigurationAvailable_ = true; Q_EMIT s.monitorConfigurationAvailableChanged(true); }
    static void connected(Session &s) { s.setState(Session::State::Connected); }
    static void monitors(Session &s, QList<MonitorInfo> m) { s.monitors_ = m; Q_EMIT s.monitorsChanged(); }
    static void ready(Session &session, bool ready) { session.setInputReady(ready); }
};
struct WidgetTestAccess {
    static QString status(const Widget &w) { return w.statusText_; }
    static QImage frame(const Widget &w) { return w.frame_; }
    static bool canSync(const Widget &widget) { return widget.canSynchronizeLocks(); }
    static void simulateCapture(Widget &widget) {
        widget.keyboardRequested_ = true;
        widget.setKeyboardCaptureState(Widget::KeyboardCaptureState::Active);
    }
};
}
class PaintRegions : public QObject {
public:
    QRegion painted;
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Paint) painted += static_cast<QPaintEvent *>(event)->region();
        return false;
    }
};
class WidgetTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void partialFramesStayIndependent() {
        QtSpice::Session s; QtSpice::Widget a,b;
        a.setSession(&s); b.setSession(&s);
        QImage frame(100,80,QImage::Format_RGB32); frame.fill(Qt::red);
        QtSpice::SessionTestAccess::frame(s,frame);
        frame.setPixelColor(4,5,Qt::blue);
        frame.setPixelColor(40,50,Qt::green); // outside damage: widgets must not copy it
        QtSpice::SessionTestAccess::frame(s,frame,{},QRect(4,5,1,1));
        frame.setPixelColor(40,50,Qt::red);
        QCOMPARE(QtSpice::WidgetTestAccess::frame(a),frame);
        QCOMPARE(QtSpice::WidgetTestAccess::frame(b),frame);
        QCOMPARE(s.monitorFrame({}).pixelColor(40,50),QColor(Qt::green));
    }
    void lateMonitorUsesCurrentConnectionState() {
        QtSpice::Session session;
        QtSpice::SessionTestAccess::connected(session);
        QtSpice::Widget extra;
        extra.setMonitor({0,1}); extra.setSession(&session);
        QCOMPARE(QtSpice::WidgetTestAccess::status(extra), QStringLiteral("Waiting for guest monitor…"));
        session.disconnectFromHost();
        QCOMPARE(QtSpice::WidgetTestAccess::status(extra), QStringLiteral("Disconnected"));
    }
    void monitorWindowLifecycle() {
        QtSpice::Session session;
        QMainWindow primary;
        auto *view = primary.menuBar()->addMenu(QStringLiteral("View"));
        new MonitorWindows(&primary, &session, view);
        QtSpice::SessionTestAccess::monitors(session, {{{0,0}, {0,0,800,600}, true}, {{0,1}, {800,0,800,600}, true}});
        QMainWindow *extra = nullptr;
        for (auto *w : QApplication::topLevelWidgets()) if (w->windowTitle() == QStringLiteral("QtSpice · Monitor 0:1")) extra = qobject_cast<QMainWindow *>(w);
        QVERIFY(extra); QVERIFY(extra->isVisible());
        extra->close(); QVERIFY(!extra->isVisible());
        QtSpice::SessionTestAccess::monitors(session, {{{0,0}, {0,0,800,600}, true}, {{0,1}, {}, false}});
        QVERIFY(!extra->isVisible());
    }
    void reconnectRetainsWindowsAndRestoresRequestedLayout() {
        QtSpice::Session session; QMainWindow primary;
        auto *view = primary.menuBar()->addMenu(QStringLiteral("View"));
        new MonitorWindows(&primary, &session, view);
        QtSpice::SessionTestAccess::connected(session);
        const QList<QtSpice::MonitorInfo> original{{{0,0},{0,0,800,600},true}, {{0,1},{800,0,800,600},true}};
        QtSpice::SessionTestAccess::monitors(session, original);
        QPointer<QMainWindow> extra;
        for (auto *w : QApplication::topLevelWidgets()) if (w->windowTitle() == QStringLiteral("QtSpice · Monitor 0:1")) extra = qobject_cast<QMainWindow *>(w);
        QVERIFY(extra); extra->resize(750, 550); auto geometry = extra->geometry();
        auto desired = original; desired[1].geometry.setSize({1024,768}); session.requestMonitorLayout(desired);
        session.disconnectFromHost(); QVERIFY(extra->isVisible()); QCOMPARE(extra->geometry(), geometry);
        QSignalSpy requests(&session, &QtSpice::Session::monitorLayoutRequested);
        QtSpice::SessionTestAccess::connected(session);
        QtSpice::SessionTestAccess::monitors(session, original);
        QtSpice::SessionTestAccess::available(session);
        QVERIFY(!requests.isEmpty());
        QCOMPARE(qvariant_cast<QList<QtSpice::MonitorInfo>>(requests.last().first()), desired);
        QCOMPARE(extra->geometry(), geometry);
    }
    void independentMonitorFramesAndClipboard() {
        QtSpice::Session session;
        QtSpice::Widget first, second;
        first.setAutoResizeGuest(false); second.setAutoResizeGuest(false);
        first.setSession(&session); second.setMonitor({0,1}); second.setSession(&session);
        QCOMPARE(session.findChildren<QObject *>(QStringLiteral("sessionClipboard")).size(), 1);
        QImage a(10,10,QImage::Format_RGB32); a.fill(Qt::red);
        QImage b(20,10,QImage::Format_RGB32); b.fill(Qt::blue);
        QtSpice::SessionTestAccess::frame(session, a); QtSpice::SessionTestAccess::frame(session, b, {0,1});
        QCOMPARE(QtSpice::WidgetTestAccess::frame(first), a);
        QCOMPARE(QtSpice::WidgetTestAccess::frame(second), b);
        QtSpice::SessionTestAccess::frame(session, {}, {0,1});
        QVERIFY(QtSpice::WidgetTestAccess::frame(second).isNull());
        QCOMPARE(QtSpice::WidgetTestAccess::frame(first), a);
    }
    void keyboardEscapeAndShortcuts() {
        using S = QtSpice::Widget::KeyboardCaptureState;
        QtSpice::Widget widget;
        widget.setKeyboardCaptureEnabled(true);
        QtSpice::WidgetTestAccess::simulateCapture(widget);
        QKeyEvent shortcut(QEvent::ShortcutOverride, Qt::Key_Tab, Qt::AltModifier);
        shortcut.ignore();
        QCoreApplication::sendEvent(&widget, &shortcut);
        QVERIFY(shortcut.isAccepted());
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
        QCoreApplication::sendEvent(&widget, &escape);
        QCOMPARE(widget.keyboardCaptureState(), S::Suspended);
        QVERIFY(widget.keyboardCaptureEnabled());
        QKeyEvent trailing(QEvent::KeyRelease, Qt::Key_G, Qt::NoModifier);
        QCoreApplication::sendEvent(&widget, &trailing);
        QCOMPARE(widget.keyboardCaptureState(), S::Suspended);
        QKeyEvent ordinary(QEvent::ShortcutOverride, Qt::Key_Tab, Qt::AltModifier);
        ordinary.ignore();
        QCoreApplication::sendEvent(&widget, &ordinary);
        QVERIFY(!ordinary.isAccepted());
        QtSpice::WidgetTestAccess::simulateCapture(widget);
        QKeyEvent altgr(QEvent::KeyPress, Qt::Key_G, Qt::ControlModifier | Qt::AltModifier | Qt::GroupSwitchModifier);
        QCoreApplication::sendEvent(&widget, &altgr);
        QCOMPARE(widget.keyboardCaptureState(), S::Active);
        widget.releaseInputCapture();
    }
    void keyboardCaptureLifecycle() {
        using S = QtSpice::Widget::KeyboardCaptureState;
        QtSpice::Widget widget;
        QCOMPARE(widget.keyboardCaptureState(), S::Disabled);
        QSignalSpy preference(&widget, &QtSpice::Widget::keyboardCaptureEnabledChanged);
        widget.setKeyboardCaptureEnabled(true);
        QCOMPARE(preference.size(), 1);
        QCOMPARE(widget.keyboardCaptureState(), S::WaitingForFocus);
        widget.releaseInputCapture();
        QVERIFY(widget.keyboardCaptureEnabled());
        QCOMPARE(widget.keyboardCaptureState(), S::Suspended);
        widget.show();
        widget.setFocus();
        QCoreApplication::processEvents();
        QCOMPARE(widget.keyboardCaptureState(), S::Suspended);
        QTest::mouseClick(&widget, Qt::LeftButton);
        QCOMPARE(widget.keyboardCaptureState(), S::WaitingForFocus);
        widget.releaseInputCapture();
        widget.setKeyboardCaptureEnabled(false);
        QCOMPARE(widget.keyboardCaptureState(), S::Disabled);
        widget.setKeyboardCaptureEnabled(false);
        QCOMPARE(preference.size(), 2);
    }
    void keyboardCaptureMenu() {
        QTemporaryDir dir;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QCoreApplication::setOrganizationName(QStringLiteral("CaptureTest"));
        QCoreApplication::setApplicationName(QStringLiteral("viewer"));
        QSettings().clear();
        QtSpice::Widget widget;
        QMenuBar menu;
        QStatusBar status;
        addInputMenu(&menu, &status, &widget);
        auto *action = menu.findChild<QAction *>(QStringLiteral("captureKeyboard"));
        QVERIFY(!action->isChecked());
        action->trigger();
        QVERIFY(widget.keyboardCaptureEnabled());
        widget.releaseInputCapture();
        QVERIFY(action->isChecked());
        QtSpice::Widget second;
        QMenuBar secondMenu;
        addInputMenu(&secondMenu, &status, &second);
        QVERIFY(second.keyboardCaptureEnabled());
        widget.setKeyboardCaptureEnabled(false);
        QVERIFY(!action->isChecked());
        QVERIFY(second.keyboardCaptureEnabled());
    }
    void lockSynchronizationFocusAndReadiness() {
        QtSpice::Session first, second;
        QtSpice::Widget widget;
        widget.setSession(&first);
        widget.show();
        widget.activateWindow();
        widget.setFocus();
        QTRY_VERIFY(widget.isActiveWindow());
        QTRY_VERIFY(widget.hasFocus());
        QVERIFY(!QtSpice::WidgetTestAccess::canSync(widget));
        QtSpice::SessionTestAccess::ready(first, true);
        QVERIFY(QtSpice::WidgetTestAccess::canSync(widget));
        widget.setSynchronizeLockKeys(false);
        QVERIFY(!QtSpice::WidgetTestAccess::canSync(widget));
        widget.setSynchronizeLockKeys(true);
        QVERIFY(QtSpice::WidgetTestAccess::canSync(widget));
        widget.setKeyboardCaptureEnabled(true);
        widget.releaseInputCapture();
        widget.setFocus();
        QVERIFY(!QtSpice::WidgetTestAccess::canSync(widget));
        QTest::mouseClick(&widget, Qt::LeftButton);
        QVERIFY(QtSpice::WidgetTestAccess::canSync(widget));
        widget.clearFocus();
        QVERIFY(!QtSpice::WidgetTestAccess::canSync(widget));
        widget.setFocus();
        QVERIFY(QtSpice::WidgetTestAccess::canSync(widget));
        QtSpice::SessionTestAccess::ready(first, false);
        QVERIFY(!QtSpice::WidgetTestAccess::canSync(widget));
        QtSpice::SessionTestAccess::ready(first, true);
        QVERIFY(QtSpice::WidgetTestAccess::canSync(widget));
        widget.setSession(&second);
        QVERIFY(!QtSpice::WidgetTestAccess::canSync(widget));
        QtSpice::SessionTestAccess::ready(second, true);
        QVERIFY(QtSpice::WidgetTestAccess::canSync(widget));
        widget.hide();
        QVERIFY(!QtSpice::WidgetTestAccess::canSync(widget));
    }
    void keyboardOptionsMenu() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QCoreApplication::setOrganizationName(QStringLiteral("KeyboardOptionsTest"));
        QCoreApplication::setApplicationName(QStringLiteral("viewer-a"));
        QtSpice::Session session;
        QtSpice::Widget widget;
        QMenuBar menu;
        QStatusBar status;
        addInputMenu(&menu, &status, &widget, &session);
        auto *send = menu.findChild<QAction *>(QStringLiteral("sendCtrlAltDelete"));
        auto *sync = menu.findChild<QAction *>(QStringLiteral("synchronizeLockKeys"));
        QVERIFY(send && sync);
        QVERIFY(!send->isEnabled());
        QVERIFY(send->shortcut().isEmpty());
        Q_EMIT session.inputReadyChanged(true);
        QVERIFY(send->isEnabled());
        send->trigger(); // Unavailable worker channel safely ignores the command.
        Q_EMIT session.inputReadyChanged(false);
        QVERIFY(!send->isEnabled());
        QVERIFY(widget.synchronizeLockKeys());
        QVERIFY(sync->isChecked());
        QSignalSpy changed(&widget, &QtSpice::Widget::synchronizeLockKeysChanged);
        sync->trigger();
        QVERIFY(!widget.synchronizeLockKeys());
        QCOMPARE(changed.size(), 1);
        QtSpice::Widget second;
        QMenuBar secondMenu;
        addInputMenu(&secondMenu, &status, &second);
        QVERIFY(!second.synchronizeLockKeys());
        QCoreApplication::setApplicationName(QStringLiteral("viewer-b"));
        QtSpice::Widget third;
        QMenuBar thirdMenu;
        addInputMenu(&thirdMenu, &status, &third);
        QVERIFY(third.synchronizeLockKeys());
        widget.setSynchronizeLockKeys(true);
        QVERIFY(sync->isChecked());
    }
    void scalingMenu() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName(QStringLiteral("ScalingMenuTest"));
        QCoreApplication::setApplicationName(QStringLiteral("viewer"));
        QtSpice::Widget widget;
        QMenu menu;
        addScalingMenu(&menu, &widget);
        auto *scaling = menu.actions().first()->menu();
        const auto actions = scaling->actions();
        QCOMPARE(actions.size(), 3);
        QVERIFY(actions[0]->isChecked());
        actions[2]->trigger();
        QCOMPARE(widget.scalingMode(), QtSpice::Widget::ScalingMode::NativePixels);
        QVERIFY(actions[2]->isChecked());
        QVERIFY(!actions[0]->isChecked());
        QCOMPARE(ViewerSettings::scalingMode(), 2);
        QtSpice::Widget restored;
        QMenu second;
        addScalingMenu(&second, &restored);
        QCOMPARE(restored.scalingMode(), QtSpice::Widget::ScalingMode::NativePixels);
        actions[1]->trigger();
        QCOMPARE(widget.scalingMode(), QtSpice::Widget::ScalingMode::UnfilteredFit);
        QVERIFY(!actions[2]->isChecked());
    }
    void physicalGeometry_data() {
        QTest::addColumn<qreal>("dpr");
        for (qreal ratio : {1.0, 1.25, 1.5, 2.0}) QTest::newRow(qPrintable(QString::number(ratio))) << ratio;
    }
    void physicalGeometry() {
        QFETCH(qreal, dpr);
        const QSize frame(1024, 768);
        const QRectF native = QtSpice::displayRect(frame, QSize(400, 300), dpr, true, {17, 29});
        QCOMPARE(native.width()*dpr, 1024.0);
        QCOMPARE(native.height()*dpr, 768.0);
        QCOMPARE(QtSpice::displayPoint(QPointF(0, 0), frame, native), QPoint(17, 29));
        const QRectF centered = QtSpice::displayRect(QSize(20, 20), QSize(400, 300), dpr, true);
        QCOMPARE(centered.width()*dpr, 20.0);
        QCOMPARE(centered.x()*dpr, qreal((qFloor(400*dpr)-20)/2));
        const QRectF fit = QtSpice::displayRect(frame, QSize(400, 400), dpr, false);
        QCOMPARE(QtSpice::displayPoint(fit.center(), frame, fit), QPoint(512, 384));
        QCOMPARE(QtSpice::displayPoint(fit.bottomRight()+QPointF(10,10), frame, fit), QPoint(1023,767));
    }
    void resizeInvalidatesWholeViewport_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("smooth") << int(QtSpice::Widget::ScalingMode::SmoothFit);
        QTest::newRow("unfiltered") << int(QtSpice::Widget::ScalingMode::UnfilteredFit);
        QTest::newRow("native") << int(QtSpice::Widget::ScalingMode::NativePixels);
    }
    void resizeInvalidatesWholeViewport() {
        QFETCH(int, mode);
        QtSpice::Session session;
        QtSpice::Widget widget;
        widget.setAutoResizeGuest(false); widget.setSession(&session);
        widget.setScalingMode(QtSpice::Widget::ScalingMode(mode));
        widget.resize(500,300); widget.show();
        QImage image(320,240,QImage::Format_RGB32); image.fill(Qt::red);
        QtSpice::SessionTestAccess::frame(session,image);
        QTest::qWait(30);
        PaintRegions regions; widget.installEventFilter(&regions);
        // Deliver the resize notification independently of the platform's own
        // exposure repaint: the handler must invalidate the old transform itself.
        QResizeEvent resize(widget.size(),QSize(400,240));
        QApplication::sendEvent(&widget,&resize);
        QTRY_VERIFY(regions.painted.contains(widget.rect()));
        QCOMPARE(QtSpice::WidgetTestAccess::frame(widget),image);
    }
    void filteredRenderingAndNativePixels() {
        QtSpice::Session session;
        QtSpice::Widget widget;
        widget.setAutoResizeGuest(false);
        widget.setSession(&session);
        widget.resize(17, 17);
        widget.show();
        QImage frame(2, 2, QImage::Format_RGB32);
        frame.fill(Qt::black);
        frame.setPixelColor(1, 0, Qt::white);
        frame.setPixelColor(1, 1, Qt::white);
        QtSpice::SessionTestAccess::frame(session, frame);
        auto hasGray = [](const QImage &image) {
            for (int y=0; y<image.height(); ++y)
                for (int x=0; x<image.width(); ++x) {
                    const int r = image.pixelColor(x,y).red();
                    if (r > 10 && r < 245) return true;
                }
            return false;
        };
        QCOMPARE(widget.scalingMode(), QtSpice::Widget::ScalingMode::SmoothFit);
        QVERIFY(hasGray(widget.grab().toImage()));
        widget.setScalingMode(QtSpice::Widget::ScalingMode::UnfilteredFit);
        QVERIFY(!hasGray(widget.grab().toImage()));
        widget.setScalingMode(QtSpice::Widget::ScalingMode::NativePixels);
        const QImage native = widget.grab().toImage();
        QVERIFY(!hasGray(native));
        int white = 0;
        for (int y=0; y<native.height(); ++y)
            for (int x=0; x<native.width(); ++x)
                if (native.pixelColor(x,y) == QColor(Qt::white)) ++white;
        QCOMPARE(white, 2);
        QCOMPARE(frame.pixelColor(1,1), QColor(Qt::white));
    }
    void nativeScrollbarsAndGuestResize() {
        QtSpice::Session session;
        QtSpice::Widget widget;
        widget.setSession(&session);
        widget.resize(400, 300);
        widget.show();
        QSignalSpy requests(&widget, &QtSpice::Widget::guestResizeRequested);
        widget.setScalingMode(QtSpice::Widget::ScalingMode::NativePixels);
        QImage frame(2048, 1536, QImage::Format_RGB32);
        frame.fill(Qt::red);
        QtSpice::SessionTestAccess::frame(session, frame);
        const auto bars = widget.findChildren<QScrollBar *>();
        QCOMPARE(bars.size(), 2);
        for (auto *bar : bars) {
            QVERIFY(bar->isVisible());
            const int total = bar->orientation() == Qt::Horizontal ? frame.width() : frame.height();
            QCOMPARE(bar->maximum()+bar->pageStep(), total);
            bar->setValue(bar->maximum());
        }
        QTRY_COMPARE_WITH_TIMEOUT(requests.size(), 1, 1000);
        QCOMPARE(requests.takeFirst().at(0).toSize(), QSize(qFloor(widget.width() * widget.devicePixelRatioF()),
                                                        qFloor(widget.height() * widget.devicePixelRatioF())));
        QVERIFY(widget.autoResizeGuest());
        widget.setAutoResizeGuest(false);
        widget.resize(500, 350);
        QTest::qWait(350);
        QCOMPARE(requests.size(), 0);
        widget.setAutoResizeGuest(true);
        QTRY_COMPARE_WITH_TIMEOUT(requests.size(), 1, 1000);
        QCOMPARE(requests.takeFirst().at(0).toSize(), QSize(qFloor(widget.width() * widget.devicePixelRatioF()),
                                                        qFloor(widget.height() * widget.devicePixelRatioF())));
        widget.setScalingMode(QtSpice::Widget::ScalingMode::SmoothFit);
        for (auto *bar : bars) { QVERIFY(!bar->isVisible()); QCOMPARE(bar->value(), 0); }
        QTRY_COMPARE_WITH_TIMEOUT(requests.size(), 1, 1000);
    }
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
