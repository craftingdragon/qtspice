#pragma once
#include <QWidget>
#include <QImage>
#include <QPointer>

namespace QtSpice {
class Session;
class Widget final : public QWidget {
    Q_OBJECT
public:
    explicit Widget(QWidget *parent = nullptr);
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
private:
    QPointer<Session> session_;
    QImage frame_;
    static int buttonMask(Qt::MouseButtons);
    static int spiceButton(Qt::MouseButton);
};
}
