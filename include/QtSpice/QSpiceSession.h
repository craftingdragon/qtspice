#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QImage>
#include <QSize>
#include <QRect>
#include <QRegion>
#include <QMap>
#include <compare>
#include <QtSpice/QSpiceConnection.h>
#include <memory>
#include <QStringList>
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

struct AudioInputDeviceInfo {
    QByteArray id;
    QString description;
};

struct FileTransferInfo {
    enum class State { Queued, Sending, Cancelling, Completed, Failed, Cancelled };
    quint64 id = 0;
    QString sourcePath, filename;
    quint64 transferredBytes = 0, totalBytes = 0;
    QString error;
    State state = State::Queued;
};

struct UsbDeviceInfo {
    enum class State { Available, Connecting, Connected, Disconnecting };
    quint64 id = 0;
    QString description;
    bool inputDevice = false;
    State state = State::Available;
    bool canRedirect = false;
    QString reason;
    bool operator==(const UsbDeviceInfo &) const = default;
};

class Session final : public QObject {
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Connected };
    Q_ENUM(State)
    enum class ImageCompression { ServerDefault, Automatic, Uncompressed };
    Q_ENUM(ImageCompression)
    ImageCompression imageCompression() const { return imageCompression_; }
    void setImageCompression(ImageCompression preference);
    // Request status only: the server does not confirm its selected algorithm.
    QString imageCompressionStatus() const;
    enum class MouseMode { Client, Server };
    Q_ENUM(MouseMode)

    enum class MicrophoneState { Disabled, WaitingForChannel, WaitingForStream, Recording, Error };
    Q_ENUM(MicrophoneState)
    // Enable only while connected. Disconnect resets consent; selection survives.
    bool microphoneEnabled() const;
    void setMicrophoneEnabled(bool);
    // Empty device ID follows the host default microphone.
    QByteArray microphoneDevice() const;
    void setMicrophoneDevice(const QByteArray &);
    QList<AudioInputDeviceInfo> audioInputDevices() const;
    MicrophoneState microphoneState() const;
    QString microphoneError() const;
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
    bool fileTransferReady() const;
    QString fileTransferReadinessReason() const;
    QList<FileTransferInfo> fileTransfers() const;
    bool sendFiles(const QStringList &paths, QString *error = nullptr);
    void cancelFileTransfer(quint64 id);
    void cancelAllFileTransfers();
    void clearFinishedFileTransfers();
    QList<UsbDeviceInfo> usbDevices() const;
    int usbFreeChannels() const;
    QString usbError() const;
    void connectUsbDevice(quint64 id);
    void disconnectUsbDevice(quint64 id);
    void cancelUsbConnection();
    State state() const noexcept;
    MouseMode mouseMode() const noexcept;
    bool inputReady() const noexcept;
    enum class ClipboardMode { Bidirectional, HostToGuest, GuestToHost, Disabled };
    Q_ENUM(ClipboardMode)
    ClipboardMode clipboardMode() const noexcept { return clipboardMode_; }
    void setClipboardMode(ClipboardMode mode);
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
    void imageCompressionChanged(QtSpice::Session::ImageCompression preference);
    void imageCompressionStatusChanged(const QString &status);
    void fileTransfersChanged();
    void fileTransferReadinessChanged();
    void usbChanged();
    void audioChanged();
    void microphoneChanged();
    void sharingChanged();
    void stateChanged(QtSpice::Session::State state);
    void errorOccurred(const QString &message);
    void channelAdded(int type, int id);
    void channelRemoved(int type, int id);
    // Cache notification; damage is in monitor pixels. Null cache means cleared.
    void monitorFrameUpdated(QtSpice::MonitorId id, const QRegion &damage);
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
    void clipboardModeChanged(QtSpice::Session::ClipboardMode mode);
    void clipboardReadyChanged(bool ready);
    void guestClipboardContent(const QtSpice::ClipboardContent &content);
    void guestClipboardText(const QString &text);

private:
    ImageCompression imageCompression_ = ImageCompression::ServerDefault;
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
    ClipboardMode clipboardMode_ = ClipboardMode::Bidirectional;
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
