#pragma once
#include <QWidget>
#include <QtSpice/QSpiceSession.h>
#include <QImage>
#include <QPointer>
#include <QString>
#include <QCursor>
#include <QTimer>
#include <memory>

class QScrollBar;

namespace QtSpice {
class Session;
class WaylandPointerLock;
class ClipboardBridge;
class IntervalStats;
class KeyboardCapture;
class KeyboardLocks;
class LockSynchronizer;
class Widget final : public QWidget {
    Q_OBJECT
public:
    enum class KeyboardCaptureState { Disabled, WaitingForFocus, Pending, Active, Suspended, Unavailable, Failed };
    Q_ENUM(KeyboardCaptureState)
    bool keyboardCaptureEnabled() const { return keyboardCaptureEnabled_; }
    void setKeyboardCaptureEnabled(bool enabled);
    KeyboardCaptureState keyboardCaptureState() const { return keyboardCaptureState_; }
    void releaseInputCapture();
    bool synchronizeLockKeys() const { return synchronizeLockKeys_; }
    void setSynchronizeLockKeys(bool enabled);
    bool lockSynchronizationAvailable() const { return lockSynchronizationAvailable_; }
    enum class ScalingMode { SmoothFit, UnfilteredFit, NativePixels };
    Q_ENUM(ScalingMode)
    ScalingMode scalingMode() const noexcept;
    void setScalingMode(ScalingMode mode);
    explicit Widget(QWidget *parent = nullptr);
    ~Widget() override;
    void setSession(Session *session);
    void setMonitor(MonitorId id);
    MonitorId monitor() const { return monitor_; }
    bool autoResizeGuest() const noexcept;
    void setAutoResizeGuest(bool enabled);
    QSize sizeHint() const override;
Q_SIGNALS:
    void monitorChanged(QtSpice::MonitorId id);
    void synchronizeLockKeysChanged(bool enabled);
    void lockSynchronizationAvailableChanged(bool available);
    void keyboardCaptureEnabledChanged(bool enabled);
    void keyboardCaptureStateChanged(QtSpice::Widget::KeyboardCaptureState state);
    void scalingModeChanged(ScalingMode mode);
    void autoResizeGuestChanged(bool enabled);
    void guestResizeRequested(QSize size);
protected:
    bool event(QEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void hideEvent(QHideEvent *) override;
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;
private:
    friend struct WidgetTestAccess;
    bool eventFilter(QObject *, QEvent *) override;
    bool synchronizeLockKeys_ = true;
    bool lockSynchronizationAvailable_ = false;
    bool lockSyncScheduled_ = false;
    std::unique_ptr<KeyboardLocks> keyboardLocks_;
    std::unique_ptr<LockSynchronizer> lockSynchronizer_;
    void scheduleLockSync();
    bool canSynchronizeLocks() const;
    bool keyboardCaptureEnabled_ = false;
    bool keyboardSuspended_ = false;
    bool keyboardRequested_ = false;
    KeyboardCaptureState keyboardCaptureState_ = KeyboardCaptureState::Disabled;
    std::unique_ptr<KeyboardCapture> keyboardCapture_;
    void updateKeyboardCapture();
    void stopKeyboardCapture();
    void setKeyboardCaptureState(KeyboardCaptureState state);
    std::unique_ptr<IntervalStats> diagnostics_;
    ScalingMode scalingMode_ = ScalingMode::SmoothFit;
    QScrollBar *horizontal_;
    QScrollBar *vertical_;
    QSize contentSize_;
    void updateDisplayGeometry();
    QRectF frameRect() const;
    MonitorId monitor_;
    QPointer<Session> session_;
    QImage frame_;
    QString statusText_ = QStringLiteral("Disconnected");
    QCursor guestCursor_;
    bool mouseGrabbed_ = false;
    bool hasLastMouse_ = false;
    QPoint lastMouse_;
    std::unique_ptr<WaylandPointerLock> waylandLock_;
    QPointer<ClipboardBridge> clipboardBridge_;
    QTimer resizeTimer_;
    QSize lastRequestedSize_;
    bool autoResizeGuest_ = true;
    double relativeRemainderX_ = 0;
    double relativeRemainderY_ = 0;
    void releaseMouseGrab();
    void scheduleGuestResize();
    static int buttonMask(Qt::MouseButtons);
    static int spiceButton(Qt::MouseButton);
};
}
