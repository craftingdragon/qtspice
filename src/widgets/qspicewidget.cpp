#include <QtSpice/QSpiceWidget.h>
#include <QtSpice/QSpiceSession.h>
#include <QtSpice/QSpiceDisplay.h>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCursor>

namespace QtSpice {
Widget::Widget(QWidget *parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}
void Widget::setSession(Session *session) {
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
        else { statusText_ = QStringLiteral("Disconnected"); frame_ = {}; }
        update();
    });
    connect(session_, &Session::errorOccurred, this, [this](const QString &message) {
        statusText_ = message;
        update();
    });
    connect(session_, &Session::cursorChanged, this, [this](const QImage &image, int hotX, int hotY) {
        if (image.isNull()) unsetCursor();
        else setCursor(QCursor(QPixmap::fromImage(image), hotX, hotY));
    });
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
    if (session_ && !frame_.isNull()) {
        QPoint p = guestPoint(event->position().toPoint(), frame_.size(), size());
        session_->sendPointer(p.x(), p.y(), buttonMask(event->buttons()));
    }
}
void Widget::mousePressEvent(QMouseEvent *event) {
    setFocus();
    mouseMoveEvent(event);
    if (session_ && spiceButton(event->button())) session_->sendButton(spiceButton(event->button()), true, buttonMask(event->buttons()));
}
void Widget::mouseReleaseEvent(QMouseEvent *event) {
    if (session_ && spiceButton(event->button())) session_->sendButton(spiceButton(event->button()), false, buttonMask(event->buttons()));
    mouseMoveEvent(event);
}
void Widget::wheelEvent(QWheelEvent *event) {
    if (!session_) return;
    int button = event->angleDelta().y() > 0 ? 4 : 5;
    int steps = qAbs(event->angleDelta().y()) / 120;
    for (int i = 0; i < steps; ++i) {
        session_->sendButton(button, true, buttonMask(event->buttons()));
        session_->sendButton(button, false, buttonMask(event->buttons()));
    }
    event->accept();
}
void Widget::keyPressEvent(QKeyEvent *event) {
    if (session_ && !event->isAutoRepeat()) session_->sendKey(xtScanCode(event->key()), true);
    event->accept();
}
void Widget::keyReleaseEvent(QKeyEvent *event) {
    if (session_ && !event->isAutoRepeat()) session_->sendKey(xtScanCode(event->key()), false);
    event->accept();
}
}
