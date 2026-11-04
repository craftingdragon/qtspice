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
