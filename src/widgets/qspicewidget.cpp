#include <QtSpice/QSpiceWidget.h>
#include <QtSpice/QSpiceSession.h>
#include <QtSpice/QSpiceDisplay.h>
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
    if (session_) session_->releaseAllInputs();
    releaseMouseGrab();
    guestCursor_ = QCursor();
    unsetCursor();
    if (session_) disconnect(session_, nullptr, this, nullptr);
    session_ = session;
    frame_ = {};
    statusText_ = QStringLiteral("Disconnected");
    update();
    if (!session_) return;
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
        QGuiApplication::platformName() == QStringLiteral("xcb") && !mouseGrabbed_) {
        grabMouse();
        mouseGrabbed_ = QWidget::mouseGrabber() == this;
        if (!mouseGrabbed_) return;
        justGrabbed = true;
        setCursor(Qt::BlankCursor);
        if (qEnvironmentVariable("XDG_SESSION_TYPE") == QStringLiteral("wayland")) {
            lastMouse_ = event->position().toPoint();
            hasLastMouse_ = true;
        } else {
            QCursor::setPos(mapToGlobal(rect().center()));
        }
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
    if (session_) session_->releaseAllInputs();
    releaseMouseGrab();
    QWidget::hideEvent(event);
}
}
