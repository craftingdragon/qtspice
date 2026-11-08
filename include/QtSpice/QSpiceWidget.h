// SPDX-License-Identifier: LGPL-3.0-or-later
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
/** A GUI-thread monitor view. Attach a non-owned Session; no connection is
 * created automatically. Defaults: monitor {0,0}, SmoothFit, automatic resize
 * and lock synchronization enabled, keyboard capture disabled. Attached views
 * share one session clipboard bridge. See docs/embedding.md.
 */
class Widget final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QtSpice::Session *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QtSpice::MonitorId monitor READ monitor WRITE setMonitor NOTIFY monitorChanged)
    Q_PROPERTY(ScalingMode scalingMode READ scalingMode WRITE setScalingMode NOTIFY scalingModeChanged)
    Q_PROPERTY(bool autoResizeGuest READ autoResizeGuest WRITE setAutoResizeGuest NOTIFY autoResizeGuestChanged)
    Q_PROPERTY(bool keyboardCaptureEnabled READ keyboardCaptureEnabled WRITE setKeyboardCaptureEnabled NOTIFY keyboardCaptureEnabledChanged)
    Q_PROPERTY(KeyboardCaptureState keyboardCaptureState READ keyboardCaptureState NOTIFY keyboardCaptureStateChanged)
    Q_PROPERTY(bool synchronizeLockKeys READ synchronizeLockKeys WRITE setSynchronizeLockKeys NOTIFY synchronizeLockKeysChanged)
    Q_PROPERTY(bool lockSynchronizationAvailable READ lockSynchronizationAvailable NOTIFY lockSynchronizationAvailableChanged)
public:
    /// Actual capture outcome; enabled preference alone does not imply Active.
    enum class KeyboardCaptureState { Disabled, WaitingForFocus, Pending, Active, Suspended, Unavailable, Failed };
    Q_ENUM(KeyboardCaptureState)
    /// Capture preference (false by default).
    bool keyboardCaptureEnabled() const { return keyboardCaptureEnabled_; }
    /// Enable local shortcut inhibition when focus and platform support permit.
    void setKeyboardCaptureEnabled(bool enabled);
    /// Current platform capture state, notified by keyboardCaptureStateChanged.
    KeyboardCaptureState keyboardCaptureState() const { return keyboardCaptureState_; }
    /// Release held inputs and suspend capture until the next console click.
    void releaseInputCapture();
    /// Host lock-key synchronization preference (true by default).
    bool synchronizeLockKeys() const { return synchronizeLockKeys_; }
    /// Synchronize known host lock bits on focus/readiness when available.
    void setSynchronizeLockKeys(bool enabled);
    /// Whether host lock state can currently be queried.
    bool lockSynchronizationAvailable() const { return lockSynchronizationAvailable_; }
    /// Local aspect-preserving fit, unfiltered fit, or unscaled scrollable pixels.
    enum class ScalingMode { SmoothFit, UnfilteredFit, NativePixels };
    Q_ENUM(ScalingMode)
    /// Local display scaling; defaults to SmoothFit.
    ScalingMode scalingMode() const noexcept;
    /// Change local scaling without directly changing guest resolution.
    void setScalingMode(ScalingMode mode);
    /// Create an unattached widget. QWidget parent ownership applies.
    explicit Widget(QWidget *parent = nullptr);
    /// Detach and release input; the borrowed session is not deleted.
    ~Widget() override;
    /// Current non-owned session, or nullptr after detachment/destruction.
    Session *session() const noexcept { return session_; }
    /// Attach before or after connection. nullptr detaches; identical pointers do nothing.
    void setSession(Session *session);
    /// Select a channel/monitor pair (default {0,0}); IDs are not list indices.
    void setMonitor(MonitorId id);
    /// Selected monitor, including when not currently advertised by the guest.
    MonitorId monitor() const { return monitor_; }
    /// Whether visible widget resizing requests guest resize (default true).
    bool autoResizeGuest() const noexcept;
    /// Debounced resize requests require a cooperating guest agent/display stack.
    void setAutoResizeGuest(bool enabled);
    /// Cached frame size, or 800x600 before a frame is available.
    QSize sizeHint() const override;
Q_SIGNALS:
    /// Emitted once after attachment changes, including destruction of the session.
    void sessionChanged(QtSpice::Session *session);
    /// Selected monitor changed. All preference notifications below report changes only.
    void monitorChanged(QtSpice::MonitorId id);
    void synchronizeLockKeysChanged(bool enabled);
    void lockSynchronizationAvailableChanged(bool available);
    void keyboardCaptureEnabledChanged(bool enabled);
    void keyboardCaptureStateChanged(QtSpice::Widget::KeyboardCaptureState state);
    void scalingModeChanged(ScalingMode mode);
    void autoResizeGuestChanged(bool enabled);
    /// Request notification, not confirmation that the guest applied the size.
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
    void attachSession(Session *session);
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
