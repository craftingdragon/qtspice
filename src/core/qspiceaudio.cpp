#include "qspiceaudio.h"
#include <QAudioDevice>
#include <QtEndian>
#include <algorithm>
#include <cmath>

namespace QtSpice {
namespace {
class QtAudioOutput final : public AudioOutput {
    QAudioSink sink;
    QIODevice *device = nullptr;

  public:
    QtAudioOutput(QAudioDevice device, const QAudioFormat &format, int bytes)
        : sink(device, format) {
        sink.setBufferSize(bytes);
        this->device = sink.start();
    }
    ~QtAudioOutput() override { sink.reset(); }
    qint64 freeBytes() const override { return sink.bytesFree(); }
    qint64 queuedBytes() const override {
        return std::max<qint64>(0, sink.bufferSize() - sink.bytesFree());
    }
    qint64 write(const char *p, qint64 n) override {
        return device ? device->write(p, n) : -1;
    }
    bool failed() const override {
        return !device || (sink.state() == QtAudio::StoppedState &&
                           sink.error() != QtAudio::NoError);
    }
};
} // namespace
AudioPlayback::AudioPlayback(Session *o, Factory f)
    : owner(o), factory(std::move(f)) {
    nativeOutput = !factory;
    if (!factory)
        factory = [](const QAudioFormat &format,
                     int bytes) -> std::unique_ptr<AudioOutput> {
            auto device = QMediaDevices::defaultAudioOutput();
            if (device.isNull() || !device.isFormatSupported(format))
                return {};
            return std::make_unique<QtAudioOutput>(device, format, bytes);
        };
    timer.setInterval(10);
    connect(&timer, &QTimer::timeout, this, &AudioPlayback::pump);
}
AudioPlayback::~AudioPlayback() { detach(); }
void AudioPlayback::changed() { Q_EMIT owner->audioChanged(); }
void AudioPlayback::fail(const QString &message) {
    timer.stop();
    output.reset();
    queue.clear();
    pending.clear();
    state = Session::AudioState::Error;
    error = message;
    changed();
}
void AudioPlayback::open() {
    if (!enabled || !active)
        return;
    if (nativeOutput && !devices) {
        devices = std::make_unique<QMediaDevices>();
        connect(devices.get(), &QMediaDevices::audioOutputsChanged, this,
                [this] {
                    if (enabled && active) {
                        output.reset();
                        queue.clear();
                        pending.clear();
                        open();
                    }
                });
    }
    if (!format.isValid()) {
        fail(QStringLiteral("Unsupported guest audio format"));
        return;
    }
    guint latency = 200;
    if (channel)
        g_object_get(channel, "min-latency", &latency, nullptr);
    const int bytes =
        format.bytesForDuration(std::clamp(latency, 50u, 500u) * 1000ll);
    output = factory(format, bytes);
    if (!output || output->failed()) {
        fail(QStringLiteral(
            "Audio output unavailable or guest format unsupported"));
        return;
    }
    error.clear();
    state = Session::AudioState::Playing;
    timer.start();
    changed();
}
void AudioPlayback::start(int f, int channels, int rate) {
    stop();
    active = true;
    format = {};
    if (f == SPICE_AUDIO_FMT_S16 && channels > 0 && channels <= 8 &&
        rate >= 8000 && rate <= 192000) {
        format.setSampleFormat(QAudioFormat::Int16);
        format.setChannelCount(channels);
        format.setSampleRate(rate);
    }
    open();
}
void AudioPlayback::stop() {
    timer.stop();
    output.reset();
    queue.clear();
    pending.clear();
    active = false;
    error.clear();
    state = enabled ? Session::AudioState::WaitingForStream
                    : Session::AudioState::Disabled;
    changed();
}
void AudioPlayback::setEnabled(bool v) {
    if (enabled == v)
        return;
    enabled = v;
    output.reset();
    queue.clear();
    pending.clear();
    timer.stop();
    error.clear();
    state = enabled ? Session::AudioState::WaitingForStream
                    : Session::AudioState::Disabled;
    if (enabled && active)
        open();
    changed();
}
void AudioPlayback::data(const void *p, int n) {
    if (!enabled || !active || !output || n <= 0)
        return;
    const int frame = format.bytesPerFrame();
    if (n % frame) {
        fail(QStringLiteral("Misaligned guest audio data"));
        return;
    }
    const int limit = format.bytesForDuration(1000000) -
                      ((pending.size() + frame - 1) / frame) * frame;
    if (n >= limit) {
        queue.clear();
        p = static_cast<const char *>(p) + n - limit;
        n = limit;
    }
    if (queue.size() + n > limit)
        queue.remove(0,
                     ((queue.size() + n - limit + frame - 1) / frame) * frame);
    queue.append(static_cast<const char *>(p), n);
    pump();
}
void AudioPlayback::pump() {
    if (!output)
        return;
    if (output->failed()) {
        fail(QStringLiteral("Audio output failed"));
        return;
    }
    const int frame = format.bytesPerFrame();
    if (pending.isEmpty()) {
        qint64 count = std::min<qint64>(queue.size(), output->freeBytes());
        count -= count % frame;
        if (count > 0) {
            pending = queue.left(count);
            queue.remove(0, count);
            gboolean guestMuted = FALSE;
            guint channels = 0;
            guint16 *gains = nullptr;
            if (channel)
                g_object_get(channel, "mute", &guestMuted, "nchannels",
                             &channels, "volume", &gains, nullptr);
            for (qsizetype i = 0; i < pending.size(); i += 2) {
                const int c = (i / 2) % format.channelCount();
                const double gain = muted || guestMuted
                                        ? 0
                                        : volume * (gains && guint(c) < channels
                                                        ? gains[c] / 65535.0
                                                        : 1.0);
                qint16 sample =
                    qFromLittleEndian<qint16>(pending.constData() + i);
                sample = qint16(
                    std::clamp(std::lround(sample * gain), -32768l, 32767l));
                // Qt's PCM output uses native byte order.
                memcpy(pending.data() + i, &sample, 2);
            }
        }
    }
    if (!pending.isEmpty()) {
        const auto n = output->write(
            pending.constData(),
            std::min<qint64>(pending.size(), output->freeBytes()));
        if (n < 0) {
            fail(QStringLiteral("Audio write failed"));
            return;
        }
        pending.remove(0, n);
    }
}
quint32 AudioPlayback::delay() const {
    if (!output || !format.isValid())
        return 0;
    return quint32(
        format.durationForBytes(bufferedBytes() + output->queuedBytes()) /
        1000);
}
void AudioPlayback::attach(SpicePlaybackChannel *c) {
    detach();
    channel = c;
    g_signal_connect(c, "playback-start",
                     G_CALLBACK(+[](SpicePlaybackChannel *, gint f, gint n,
                                    gint r, gpointer p) {
                         static_cast<AudioPlayback *>(p)->start(f, n, r);
                     }),
                     this);
    g_signal_connect(
        c, "playback-data",
        G_CALLBACK(+[](SpicePlaybackChannel *, gpointer b, gint n, gpointer p) {
            static_cast<AudioPlayback *>(p)->data(b, n);
        }),
        this);
    g_signal_connect(c, "playback-stop",
                     G_CALLBACK(+[](SpicePlaybackChannel *, gpointer p) {
                         static_cast<AudioPlayback *>(p)->stop();
                     }),
                     this);
    g_signal_connect(c, "playback-get-delay",
                     G_CALLBACK(+[](SpicePlaybackChannel *c, gpointer p) {
                         spice_playback_channel_set_delay(
                             c, static_cast<AudioPlayback *>(p)->delay());
                     }),
                     this);
    spice_channel_connect(SPICE_CHANNEL(c));
}
void AudioPlayback::detach(SpiceChannel *c) {
    if (c && c != SPICE_CHANNEL(channel))
        return;
    if (channel)
        g_signal_handlers_disconnect_by_data(channel, this);
    channel = nullptr;
    stop();
}
} // namespace QtSpice
