// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QAudioSink>
#include <QMediaDevices>
#include <QTimer>
#include <QtSpice/QSpiceSession.h>
#include <functional>
#include <spice-client.h>

namespace QtSpice {
class AudioOutput {
  public:
    virtual ~AudioOutput() = default;
    virtual qint64 freeBytes() const = 0;
    virtual qint64 queuedBytes() const = 0;
    virtual qint64 write(const char *, qint64) = 0;
    virtual bool failed() const = 0;
};
class AudioPlayback : public QObject {
  public:
    using Factory =
        std::function<std::unique_ptr<AudioOutput>(const QAudioFormat &, int)>;
    explicit AudioPlayback(Session *owner, Factory factory = {});
    ~AudioPlayback() override;
    bool enabled = true, muted = false;
    double volume = 1;
    Session::AudioState state = Session::AudioState::WaitingForStream;
    QString error;
    void attach(SpicePlaybackChannel *);
    void detach(SpiceChannel *channel = nullptr);
    void start(int format, int channels, int rate);
    void stop();
    void data(const void *, int);
    void pump();
    void setEnabled(bool);
    quint32 delay() const;
    qsizetype bufferedBytes() const { return queue.size() + pending.size(); }

  private:
    Session *owner;
    Factory factory;
    SpicePlaybackChannel *channel = nullptr;
    std::unique_ptr<QMediaDevices> devices;
    bool nativeOutput = false;
    QTimer timer;
    QAudioFormat format;
    std::unique_ptr<AudioOutput> output;
    QByteArray queue, pending;
    bool active = false;
    void open();
    void changed();
    void fail(const QString &);
};
} // namespace QtSpice

namespace QtSpice {
class AudioCapture {
public:
    virtual ~AudioCapture() = default;
    virtual QIODevice *device() const = 0;
    virtual bool failed() const = 0;
};
class AudioRecording : public QObject {
public:
    using CaptureFactory = std::function<std::unique_ptr<AudioCapture>(const QAudioDevice &, const QAudioFormat &, std::function<void()>)>;
    explicit AudioRecording(Session *);
    CaptureFactory factory;
    std::function<QAudioFormat(const QByteArray &, const QAudioFormat &)> inputFormat;
    void devicesChanged();
    ~AudioRecording() override;
    bool enabled = false;
    QByteArray deviceId;
    Session::MicrophoneState state = Session::MicrophoneState::Disabled;
    QString error;
    void attach(SpiceRecordChannel *, bool connectChannel = true);
    void detach(SpiceChannel * = nullptr);
    void setEnabled(bool);
    void select(const QByteArray &);
    void start(int, int, int);
    void stop();
    // PCM conversion seam, also used by native capture.
    bool configure(const QAudioFormat &, int channels, int rate);
    static QAudioFormat captureFormat(const QAudioFormat &preferred, const QAudioFormat &requested);
    void data(const QByteArray &);
    std::function<void(const QByteArray &, quint32)> send;
    std::function<QPair<bool, QList<quint16>>()> controls;
private:
    struct Capture;
    std::unique_ptr<Capture> capture;
    Session *owner;
    SpiceRecordChannel *channel = nullptr;
    QMediaDevices devices;
    bool requested = false;
    int guestFormat = 0, guestChannels = 0, guestRate = 0;
    quint64 generation = 0;
    quint64 permissionRequest = 0;
    void open();
    void close();
    void fail(const QString &);
    void changed();
};
}
