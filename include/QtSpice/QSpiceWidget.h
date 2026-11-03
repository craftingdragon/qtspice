#pragma once
#include <QWidget>
#include <QImage>
#include <QPointer>
#include <QString>
#include <QCursor>
#include <memory>

namespace QtSpice {
class Session;
class WaylandPointerLock;
class ClipboardBridge;
class Widget final : public QWidget {
    Q_OBJECT
public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget() override;
    void setSession(Session *session);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void hideEvent(QHideEvent *) override;
private:
    QPointer<Session> session_;
    QImage frame_;
    QString statusText_ = QStringLiteral("Disconnected");
    QCursor guestCursor_;
    bool mouseGrabbed_ = false;
    bool hasLastMouse_ = false;
    QPoint lastMouse_;
    std::unique_ptr<WaylandPointerLock> waylandLock_;
    std::unique_ptr<ClipboardBridge> clipboardBridge_;
    double relativeRemainderX_ = 0;
    double relativeRemainderY_ = 0;
    void releaseMouseGrab();
    static int buttonMask(Qt::MouseButtons);
    static int spiceButton(Qt::MouseButton);
};
}
