#pragma once

#include <QObject>
#include <QString>
#include <QImage>
#include <QSize>
#include <QRect>
#include <QMap>
#include <compare>
#include <QtSpice/QSpiceConnection.h>
#include <memory>
#include <optional>

namespace QtSpice {

struct MonitorId {
    int channelId = 0;
    int monitorId = 0;
    auto operator<=>(const MonitorId &) const = default;
};
inline size_t qHash(MonitorId id, size_t seed = 0) noexcept { return qHashMulti(seed, id.channelId, id.monitorId); }
struct MonitorCursor { QImage image; QPoint hotspot; };
struct MonitorInfo {
    MonitorId id;
    QRect geometry;
    bool enabled = false;
    bool operator==(const MonitorInfo &) const = default;
};

struct ClipboardContent {
    std::optional<QString> text;
    QImage image;
};

class Session final : public QObject {
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Connected };
    Q_ENUM(State)
    enum class MouseMode { Client, Server };
    Q_ENUM(MouseMode)

    enum class AudioState { Disabled, WaitingForStream, Playing, Error };
    Q_ENUM(AudioState)
    bool audioEnabled() const;
    bool audioMuted() const;
    double audioVolume() const;
    AudioState audioState() const;
    QString audioError() const;
    void setAudioEnabled(bool enabled);
    void setAudioMuted(bool muted);
    void setAudioVolume(double volume);
    enum class SharingState { Disabled, WaitingForChannel, Connecting, WaitingForGuest, Active, Error };
    Q_ENUM(SharingState)
    bool setSharedDirectory(const SharedDirectory &directory, QString *error = nullptr);
    void disableSharedDirectory();
    std::optional<SharedDirectory> sharedDirectory() const;
    SharingState sharingState() const noexcept;
    QString sharingError() const;

    explicit Session(QObject *parent = nullptr);
    ~Session() override;
    State state() const noexcept;
    MouseMode mouseMode() const noexcept;
    bool inputReady() const noexcept;
    bool clipboardReady() const noexcept;
    void connectToHost(const QString &host, quint16 port);
    void connectToConfig(const ConnectionConfig &config);
    void disconnectFromHost();
    enum class KeyboardLock { ScrollLock = 1, NumLock = 2, CapsLock = 4 };
    Q_ENUM(KeyboardLock)
    Q_DECLARE_FLAGS(KeyboardLocks, KeyboardLock)
    Q_FLAG(KeyboardLocks)
    void sendCtrlAltDelete();
    // Unknown bits retain the latest guest-reported state. Requests are not
    // retained across an unavailable inputs channel or reconnection.
    void setKeyLocks(KeyboardLocks locks, KeyboardLocks knownLocks);
    void sendKey(quint32 scancode, bool pressed);
    void sendPointer(int x, int y, int buttons);
    void sendRelativePointer(int dx, int dy, int buttons);
    void sendButton(int button, bool pressed, int buttons);
    void releaseAllInputs();
    void setHostClipboardContent(const ClipboardContent &content);
    void setHostClipboardText(const QString &text);
    void setDesiredDisplaySize(QSize size);
    QList<MonitorInfo> monitors() const { return monitors_; }
    MonitorCursor monitorCursor(int channelId) const { return cursors_.value(channelId); }
    QImage monitorFrame(MonitorId id) const { return frames_.value(id); }
    void setDesiredMonitorSize(MonitorId id, QSize size);
    void setMonitorEnabled(MonitorId id, bool enabled);
    void sendMonitorPointer(MonitorId id, int x, int y, int buttons);
    void requestMonitorLayout(const QList<MonitorInfo> &layout);
    bool monitorConfigurationAvailable() const { return monitorConfigurationAvailable_; }


Q_SIGNALS:
    void audioChanged();
    void sharingChanged();
    void stateChanged(QtSpice::Session::State state);
    void errorOccurred(const QString &message);
    void channelAdded(int type, int id);
    void channelRemoved(int type, int id);
    void frameReady(const QImage &frame);
    void monitorsChanged();
    void monitorLayoutRequested(const QList<QtSpice::MonitorInfo> &layout);
    void monitorConfigurationStatusChanged(const QString &status);
    void monitorConfigurationAvailableChanged(bool available);
    void monitorFrameReady(QtSpice::MonitorId id, const QImage &frame);
    void monitorCursorChanged(int channelId, const QImage &cursor, int hotX, int hotY);

    void cursorChanged(const QImage &cursor, int hotX, int hotY);
    void mouseModeChanged(QtSpice::Session::MouseMode mode);
    void inputReadyChanged(bool ready);
    void clipboardReadyChanged(bool ready);
    void guestClipboardContent(const QtSpice::ClipboardContent &content);
    void guestClipboardText(const QString &text);

private:
    QList<MonitorInfo> monitors_;
    QMap<MonitorId, QImage> frames_;
    QMap<int, MonitorCursor> cursors_;
    bool monitorConfigurationAvailable_ = false;
    SharingState sharingState_ = SharingState::Disabled;
    QString sharingError_;
    std::optional<SharedDirectory> sharedDirectory_;
    void updateSharingState();
    friend struct SessionTestAccess;
    struct Worker;
    std::unique_ptr<Worker> worker_;
    State state_ = State::Disconnected;
    MouseMode mouseMode_ = MouseMode::Client;
    bool inputReady_ = false;
    bool clipboardReady_ = false;
    void setState(State state);
    void setMouseMode(MouseMode mode);
    void setInputReady(bool ready);
    void setClipboardReady(bool ready);
};

} // namespace QtSpice

Q_DECLARE_OPERATORS_FOR_FLAGS(QtSpice::Session::KeyboardLocks)

Q_DECLARE_METATYPE(QtSpice::ClipboardContent)

Q_DECLARE_METATYPE(QtSpice::MonitorId)
