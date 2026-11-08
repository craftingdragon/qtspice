// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../examples/fullscreen.h"
#include <QtSpice/QSpiceSession.h>
#include <QtTest>

namespace QtSpice {
struct WidgetTestAccess {
    static void capture(Widget &widget) {
        widget.keyboardRequested_ = true;
        widget.setKeyboardCaptureState(Widget::KeyboardCaptureState::Active);
    }
};
}

class EnterObserver : public QObject {
public:
    int presses = 0;
    int releases = 0;
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                if (event->type() == QEvent::KeyPress) ++presses;
                else ++releases;
            }
        }
        return false;
    }
};
class FullscreenTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
private Q_SLOTS:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
        QCoreApplication::setOrganizationName(QStringLiteral("QtSpiceTest"));
        QCoreApplication::setApplicationName(QStringLiteral("fullscreen_test"));
    }
    void guestResizeFollowsFullscreen_data() {
        QTest::addColumn<bool>("nativePixels");
        QTest::newRow("fit") << false;
        QTest::newRow("native") << true;
    }
    void guestResizeFollowsFullscreen() {
        QFETCH(bool, nativePixels);
        QtSpice::Session session;
        QMainWindow window;
        auto *console = new QtSpice::Widget(&window);
        if (nativePixels) console->setScalingMode(QtSpice::Widget::ScalingMode::NativePixels);
        console->setSession(&session);
        window.setCentralWidget(console);
        auto *menu = window.menuBar()->addMenu(QStringLiteral("View"));
        Fullscreen fullscreen(&window, menu, console);
        QSignalSpy requests(console, &QtSpice::Widget::guestResizeRequested);
        window.resize(400, 300);
        window.show();
        auto expected = [console] {
            if (console->scalingMode() != QtSpice::Widget::ScalingMode::NativePixels) return console->size();
            return QSize(qFloor(console->width() * console->devicePixelRatioF()),
                         qFloor(console->height() * console->devicePixelRatioF()));
        };
        QTRY_COMPARE(requests.size(), 1);
        const QSize original = expected();
        requests.clear();
        fullscreen.setFullscreen(true);
        QTRY_COMPARE(requests.size(), 1);
        QCOMPARE(requests.last().first().toSize(), expected());
        QVERIFY(expected() != original);
        requests.clear();
        fullscreen.setFullscreen(false);
        QTRY_COMPARE(requests.size(), 1);
        QCOMPARE(requests.last().first().toSize(), original);
        console->setAutoResizeGuest(false);
        requests.clear();
        fullscreen.setFullscreen(true);
        QTest::qWait(400);
        QVERIFY(requests.isEmpty());
        fullscreen.setFullscreen(false);
        QTest::qWait(400);
        QVERIFY(requests.isEmpty());
    }
    void restoresGeometryAndChrome() {
        QMainWindow window;
        auto *console = new QtSpice::Widget(&window);
        window.setCentralWidget(console);
        auto *menu = window.menuBar()->addMenu(QStringLiteral("View"));
        window.statusBar()->showMessage(QStringLiteral("Connected"));
        Fullscreen fullscreen(&window, menu, console);
        window.setGeometry(20, 30, 400, 300);
        window.show();
        QCoreApplication::processEvents();
        const QRect original = window.geometry();
        const QSize content = console->size();
        auto *action = menu->actions().first();
        auto *exit = window.findChild<QAction *>(QStringLiteral("exitFullscreenAction"));
        QVERIFY(exit);
        QVERIFY(!exit->isEnabled());
        for (int i = 0; i < 2; ++i) {
            action->trigger();
            QTRY_VERIFY(window.isFullScreen());
            QVERIFY(action->isChecked());
            QVERIFY(window.menuBar()->isHidden());
            QVERIFY(window.statusBar()->isHidden());
            QVERIFY(exit->isEnabled());
            QVERIFY(console->autoResizeGuest());
            exit->trigger();
            QTRY_VERIFY(!window.isFullScreen());
            QTRY_COMPARE(window.geometry(), original);
            QTRY_COMPARE(console->size(), content);
            QVERIFY(!action->isChecked());
            QVERIFY(!window.menuBar()->isHidden());
            QVERIFY(!window.statusBar()->isHidden());
            QVERIFY(!exit->isEnabled());
        }
    }
    void preservesMaximizedAndHiddenBars() {
        QMainWindow window;
        auto *console = new QtSpice::Widget(&window);
        window.setCentralWidget(console);
        auto *menu = window.menuBar()->addMenu(QStringLiteral("View"));
        Fullscreen fullscreen(&window, menu, console);
        window.statusBar()->hide();
        window.showMaximized();
        QCoreApplication::processEvents();
        fullscreen.setFullscreen(true);
        QTRY_VERIFY(window.isFullScreen());
        fullscreen.setFullscreen(true); // Must not overwrite saved window state.
        fullscreen.setFullscreen(false);
        QTRY_VERIFY(window.isMaximized());
        QVERIFY(!window.isFullScreen());
        QVERIFY(window.statusBar()->isHidden());
        QVERIFY(!window.menuBar()->isHidden());
        fullscreen.setFullscreen(false);
        QVERIFY(window.isMaximized());
    }
    void captureReleaseAndShortcut() {
        using S = QtSpice::Widget::KeyboardCaptureState;
        QMainWindow window;
        auto *console = new QtSpice::Widget(&window);
        window.setCentralWidget(console);
        auto *menu = window.menuBar()->addMenu(QStringLiteral("View"));
        Fullscreen fullscreen(&window, menu, console);
        window.show();
        window.activateWindow();
        QTRY_VERIFY(window.isActiveWindow());
        console->setKeyboardCaptureEnabled(true);
        fullscreen.setFullscreen(true);
        QCOMPARE(console->keyboardCaptureState(), S::Suspended);
        QVERIFY(console->keyboardCaptureEnabled());
        console->setAutoResizeGuest(false);
        console->setFocus();
        QtSpice::WidgetTestAccess::capture(*console);
        // Active capture accepts ShortcutOverride, so F11 belongs to the guest.
        QTest::keyClick(console, Qt::Key_F11);
        QVERIFY(window.isFullScreen());
        QTest::keyClick(console, Qt::Key_Return, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(!window.isFullScreen());
        QVERIFY(console->keyboardCaptureEnabled());
        QCOMPARE(console->keyboardCaptureState(), S::Suspended);
        QVERIFY(!console->autoResizeGuest());
    }
    void reservedChordAndRepeats() {
        QMainWindow window;
        auto *console = new QtSpice::Widget(&window);
        window.setCentralWidget(console);
        auto *view = window.menuBar()->addMenu(QStringLiteral("View"));
        Fullscreen fullscreen(&window, view, console);
        EnterObserver observer;
        console->installEventFilter(&observer);
        window.show();
        const auto mods = Qt::ControlModifier | Qt::AltModifier;
        for (int key : {Qt::Key_Return, Qt::Key_Enter}) {
            fullscreen.setFullscreen(true);
            QKeyEvent altgr(QEvent::KeyPress, key, mods | Qt::GroupSwitchModifier);
            QCoreApplication::sendEvent(console, &altgr);
            QVERIFY(window.isFullScreen());
            observer.presses = observer.releases = 0;
            QKeyEvent press(QEvent::KeyPress, key, mods);
            QCoreApplication::sendEvent(console, &press);
            QVERIFY(!window.isFullScreen());
            QKeyEvent repeat(QEvent::KeyPress, key, mods, {}, true);
            QCoreApplication::sendEvent(console, &repeat);
            QKeyEvent release(QEvent::KeyRelease, key, mods);
            QCoreApplication::sendEvent(console, &release);
            QCOMPARE(observer.presses, 0);
            QCOMPARE(observer.releases, 0);
            QTest::keyClick(console, static_cast<Qt::Key>(key), mods);
            QCOMPARE(observer.presses, 1);
            QCOMPARE(observer.releases, 1);
        }
    }
    void opaqueEdgeMenuBackground() {
        FullscreenMenuBar bar;
        bar.addMenu(QStringLiteral("View"));
        bar.resize(400, 30);
        bar.ensurePolished();
        QImage image(bar.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        // Exclude QWidget's render-time background fill: the actual paint path
        // must cover the empty area too, even with a transparent menu style.
        bar.setStyleSheet(QStringLiteral("QMenuBar { background: transparent; }"));
        bar.render(&image, QPoint(), QRegion(), QWidget::DrawChildren);
        QCOMPARE(image.pixelColor(390, 15).alpha(), 255);
        QColor expected = bar.palette().color(QPalette::Window);
        expected.setAlpha(255);
        QCOMPARE(image.pixelColor(390, 15), expected);
    }
    void edgeMenu() {
        ViewerSettings::setFullscreenEdgeMenu(false);
        QMainWindow window;
        auto *console = new QtSpice::Widget(&window);
        window.setCentralWidget(console);
        auto *view = window.menuBar()->addMenu(QStringLiteral("View"));
        Fullscreen fullscreen(&window, view, console);
        window.show();
        fullscreen.setFullscreen(true);
        auto *edge = window.findChild<QAction *>(QStringLiteral("fullscreenEdgeMenuAction"));
        auto *bar = window.findChild<QMenuBar *>(QStringLiteral("fullscreenEdgeMenu"));
        QVERIFY(!edge->isChecked());
        QCoreApplication::processEvents();
        const QSize size = console->size();
        QTest::mouseMove(console, QPoint(20, 1));
        QVERIFY(!bar->isVisible());
        edge->setChecked(true);
        QVERIFY(ViewerSettings::fullscreenEdgeMenu());
        QTest::mouseMove(console, QPoint(21, 1));
        QTRY_VERIFY(bar->isVisible());
        QTRY_COMPARE(bar->pos(), QPoint(0, 0));
        QCOMPARE(console->size(), size);
        QCOMPARE(bar->actions(), window.menuBar()->actions());
        view->popup(window.mapToGlobal(QPoint(10, bar->height())));
        QTRY_VERIFY(view->isVisible());
        QTest::mouseMove(console, QPoint(20, 100));
        QTest::qWait(350);
        QVERIFY(bar->isVisible());
        view->close();
        QTRY_VERIFY(!bar->isVisible());
        QTest::mouseMove(console, QPoint(23, 1));
        QTRY_VERIFY(bar->isVisible());
        QTest::mouseMove(console, QPoint(20, 100));
        QTRY_VERIFY(!bar->isVisible());
        QTest::mouseMove(console, QPoint(22, 1));
        QTRY_VERIFY(bar->isVisible());
        edge->setChecked(false);
        QVERIFY(!bar->isVisible());
        fullscreen.setFullscreen(false);
    }
    void independentWindowsAndExternalExit() {
        QMainWindow first, second;
        auto *a = new QtSpice::Widget(&first);
        auto *b = new QtSpice::Widget(&second);
        first.setCentralWidget(a);
        second.setCentralWidget(b);
        auto *menuA = first.menuBar()->addMenu(QStringLiteral("View"));
        auto *menuB = second.menuBar()->addMenu(QStringLiteral("View"));
        Fullscreen one(&first, menuA, a), two(&second, menuB, b);
        first.show();
        second.show();
        one.setFullscreen(true);
        QVERIFY(first.isFullScreen());
        QVERIFY(!second.isFullScreen());
        QVERIFY(!menuB->actions().first()->isChecked());
        first.showNormal(); // Simulate a window-manager fullscreen exit.
        QTRY_VERIFY(!menuA->actions().first()->isChecked());
        QVERIFY(!first.menuBar()->isHidden());
        QVERIFY(!first.statusBar()->isHidden());
        QVERIFY(!first.findChild<QAction *>(QStringLiteral("exitFullscreenAction"))->isEnabled());
    }
};
QTEST_MAIN(FullscreenTest)
#include "fullscreen_test.moc"
