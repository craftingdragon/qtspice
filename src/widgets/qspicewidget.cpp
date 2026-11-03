#include <QtSpice/QSpiceWidget.h>
#include <QtSpice/QSpiceSession.h>
#include <QtSpice/QSpiceDisplay.h>
#include "clipboardbridge.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QGuiApplication>
#include <QFocusEvent>
#include <QHideEvent>
#include <cmath>
#ifdef QTSPICE_WAYLAND_POINTER_LOCK
#include "waylandpointerlock.h"
#endif

namespace QtSpice {
Widget::Widget(QWidget *parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    clipboardBridge_ = std::make_unique<ClipboardBridge>();
    resizeTimer_.setSingleShot(true);
    resizeTimer_.setInterval(250);
    connect(&resizeTimer_, &QTimer::timeout, this, [this] {
        if (!autoResizeGuest_ || !isVisible() || !session_) return;
        const QSize requested = size();
        if (requested.width() < 320 || requested.height() < 200 || requested.width() > 8192 ||
            requested.height() > 8192 || qint64(requested.width()) * requested.height() > 32 * 1024 * 1024 ||
            requested == lastRequestedSize_) return;
        lastRequestedSize_ = requested;
        Q_EMIT guestResizeRequested(requested);
        session_->setDesiredDisplaySize(requested);
    });
#ifdef QTSPICE_WAYLAND_POINTER_LOCK
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        waylandLock_ = std::make_unique<WaylandPointerLock>(
            [this](double dx, double dy) {
                relativeRemainderX_ += dx;
                relativeRemainderY_ += dy;
                const int x = static_cast<int>(std::trunc(relativeRemainderX_));
                const int y = static_cast<int>(std::trunc(relativeRemainderY_));
                relativeRemainderX_ -= x;
                relativeRemainderY_ -= y;
                if (session_ && session_->inputReady() && (x || y))
                    session_->sendRelativePointer(x, y, buttonMask(QGuiApplication::mouseButtons()));
            },
            [this](bool locked) {
                if (!locked && mouseGrabbed_ && session_) session_->releaseAllInputs();
                mouseGrabbed_ = locked;
                setCursor(locked ? QCursor(Qt::BlankCursor) : guestCursor_);
                hasLastMouse_ = false;
                relativeRemainderX_ = relativeRemainderY_ = 0;
            });
    }
#endif
}
Widget::~Widget() { releaseMouseGrab(); }
void Widget::setSession(Session *session) {
    resizeTimer_.stop();
    if (session_ && session_ != session) session_->setDesiredDisplaySize({});
    lastRequestedSize_ = {};
    clipboardBridge_->setActive(false);
    if (session_) session_->releaseAllInputs();
    releaseMouseGrab();
    guestCursor_ = QCursor();
    unsetCursor();
    if (session_) {
        disconnect(session_, nullptr, this, nullptr);
        disconnect(session_, nullptr, clipboardBridge_.get(), nullptr);
        disconnect(clipboardBridge_.get(), nullptr, session_, nullptr);
    }
    session_ = session;
    frame_ = {};
    statusText_ = QStringLiteral("Disconnected");
    update();
    if (!session_) return;
    scheduleGuestResize();
    connect(clipboardBridge_.get(), &ClipboardBridge::hostTextChanged, session_, &Session::setHostClipboardText);
    connect(session_, &Session::guestClipboardText, clipboardBridge_.get(), &ClipboardBridge::applyGuestText);
    connect(session_, &Session::clipboardReadyChanged, clipboardBridge_.get(), &ClipboardBridge::setActive);
    clipboardBridge_->setActive(session_->clipboardReady());
    connect(session_, &Session::frameReady, this, [this](const QImage &image) { frame_ = image; updateGeometry(); update(); });
    connect(session_, &Session::stateChanged, this, [this](Session::State state) {
        if (state == Session::State::Connecting) statusText_ = QStringLiteral("Connecting…");
        else if (state == Session::State::Connected) statusText_ = QStringLiteral("Waiting for display…");
        else { statusText_ = QStringLiteral("Disconnected"); frame_ = {}; releaseMouseGrab(); }
        update();
    });
    connect(session_, &Session::errorOccurred, this, [this](const QString &message) {
        statusText_ = message;
        update();
    });
    connect(session_, &Session::cursorChanged, this, [this](const QImage &image, int hotX, int hotY) {
        guestCursor_ = image.isNull() ? QCursor() : QCursor(QPixmap::fromImage(image), hotX, hotY);
        if (!mouseGrabbed_) setCursor(guestCursor_);
    });
    connect(session_, &Session::mouseModeChanged, this, [this](Session::MouseMode mode) {
        session_->releaseAllInputs();
        hasLastMouse_ = false;
        if (mode == Session::MouseMode::Client) {
            releaseMouseGrab();
            setToolTip({});
        } else {
            setToolTip(QStringLiteral("Click to capture mouse; Ctrl+Alt+G releases it"));
        }
    });
    connect(session_, &Session::inputReadyChanged, this, [this](bool ready) {
        if (!ready) releaseMouseGrab();
    });
}
bool Widget::autoResizeGuest() const noexcept { return autoResizeGuest_; }
void Widget::setAutoResizeGuest(bool enabled) {
    if (autoResizeGuest_ == enabled) return;
    autoResizeGuest_ = enabled;
    resizeTimer_.stop();
    lastRequestedSize_ = {};
    if (!enabled && session_) session_->setDesiredDisplaySize({});
    if (enabled) scheduleGuestResize();
    Q_EMIT autoResizeGuestChanged(enabled);
}
void Widget::scheduleGuestResize() {
    if (autoResizeGuest_ && session_ && isVisible()) resizeTimer_.start();
}
void Widget::releaseMouseGrab() {
#ifdef QTSPICE_WAYLAND_POINTER_LOCK
    if (waylandLock_) waylandLock_->release();
#endif
    if (mouseGrabbed_) {
        if (QGuiApplication::platformName() == QStringLiteral("xcb")) releaseMouse();
        mouseGrabbed_ = false;
        setCursor(guestCursor_);
    }
    hasLastMouse_ = false;
}
QSize Widget::sizeHint() const { return frame_.isNull() ? QSize(800, 600) : frame_.size(); }
void Widget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (!frame_.isNull()) painter.drawImage(fittedRect(frame_.size(), size()), frame_);
    else { painter.setPen(Qt::white); painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, statusText_); }
}
int Widget::buttonMask(Qt::MouseButtons buttons) {
    return ((buttons & Qt::LeftButton) ? 1 : 0) | ((buttons & Qt::MiddleButton) ? 2 : 0) | ((buttons & Qt::RightButton) ? 4 : 0);
}
int Widget::spiceButton(Qt::MouseButton button) {
    if (button == Qt::LeftButton) return 1;
    if (button == Qt::MiddleButton) return 2;
    if (button == Qt::RightButton) return 3;
    return 0;
}
void Widget::mouseMoveEvent(QMouseEvent *event) {
    if (session_ && session_->inputReady() && !frame_.isNull()) {
        if (session_->mouseMode() == Session::MouseMode::Server) {
#ifdef QTSPICE_WAYLAND_POINTER_LOCK
            if (waylandLock_) return; // Relative events arrive through the compositor while locked.
#endif
            QPoint p = event->position().toPoint();
            if (mouseGrabbed_ && QGuiApplication::platformName() == QStringLiteral("xcb") &&
                qEnvironmentVariable("XDG_SESSION_TYPE") != QStringLiteral("wayland")) {
                QPoint center = rect().center();
                QPoint delta = p - center;
                if (!delta.isNull()) {
                    session_->sendRelativePointer(delta.x(), delta.y(), buttonMask(event->buttons()));
                    QCursor::setPos(mapToGlobal(center));
                }
            } else if (hasLastMouse_) {
                QPoint delta = p - lastMouse_;
                session_->sendRelativePointer(delta.x(), delta.y(), buttonMask(event->buttons()));
            }
            lastMouse_ = p;
            hasLastMouse_ = true;
            return;
        }
        QPoint p = guestPoint(event->position().toPoint(), frame_.size(), size());
        session_->sendPointer(p.x(), p.y(), buttonMask(event->buttons()));
    }
}
void Widget::mousePressEvent(QMouseEvent *event) {
    setFocus();
    bool justGrabbed = false;
#ifdef QTSPICE_WAYLAND_POINTER_LOCK
    if (waylandLock_ && session_ && session_->inputReady() &&
        session_->mouseMode() == Session::MouseMode::Server && !mouseGrabbed_) {
        if (!waylandLock_->request(window()->windowHandle())) {
            setToolTip(QStringLiteral("Wayland compositor does not support relative pointer capture"));
        }
        return;
    }
#endif
    if (session_ && session_->inputReady() && session_->mouseMode() == Session::MouseMode::Server &&
        QGuiApplication::platformName() == QStringLiteral("xcb") &&
        qEnvironmentVariable("XDG_SESSION_TYPE") != QStringLiteral("wayland") && !mouseGrabbed_) {
        grabMouse();
        mouseGrabbed_ = QWidget::mouseGrabber() == this;
        if (!mouseGrabbed_) return;
        justGrabbed = true;
        setCursor(Qt::BlankCursor);
        QCursor::setPos(mapToGlobal(rect().center()));
    }
    if (!justGrabbed) mouseMoveEvent(event);
    if (session_ && spiceButton(event->button()))
        session_->sendButton(spiceButton(event->button()), true, buttonMask(event->buttons() & ~event->button()));
}
void Widget::mouseReleaseEvent(QMouseEvent *event) {
    if (session_ && spiceButton(event->button()))
        session_->sendButton(spiceButton(event->button()), false, buttonMask(event->buttons() | event->button()));
    mouseMoveEvent(event);
}
void Widget::wheelEvent(QWheelEvent *event) {
    if (!session_ || !session_->inputReady() || event->angleDelta().y() == 0) return;
    int button = event->angleDelta().y() > 0 ? 4 : 5;
    int steps = qAbs(event->angleDelta().y()) / 120;
    for (int i = 0; i < steps; ++i) {
        session_->sendButton(button, true, buttonMask(event->buttons()));
        session_->sendButton(button, false, buttonMask(event->buttons()));
    }
    event->accept();
}
void Widget::keyPressEvent(QKeyEvent *event) {
    if (mouseGrabbed_ && event->key() == Qt::Key_G &&
        (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) == (Qt::ControlModifier | Qt::AltModifier)) {
        if (session_) session_->releaseAllInputs();
        releaseMouseGrab();
        event->accept();
        return;
    }
    if (session_ && !event->isAutoRepeat()) session_->sendKey(nativeXtScanCode(event->nativeScanCode(), event->key()), true);
    event->accept();
}
void Widget::keyReleaseEvent(QKeyEvent *event) {
    if (session_ && !event->isAutoRepeat()) session_->sendKey(nativeXtScanCode(event->nativeScanCode(), event->key()), false);
    event->accept();
}
void Widget::focusOutEvent(QFocusEvent *event) {
    if (session_) session_->releaseAllInputs();
    releaseMouseGrab();
    QWidget::focusOutEvent(event);
}
void Widget::hideEvent(QHideEvent *event) {
    resizeTimer_.stop();
    if (session_ && autoResizeGuest_) session_->setDesiredDisplaySize({});
    lastRequestedSize_ = {};
    if (session_) session_->releaseAllInputs();
    releaseMouseGrab();
    QWidget::hideEvent(event);
}
void Widget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    scheduleGuestResize();
}
void Widget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    scheduleGuestResize();
}
}
