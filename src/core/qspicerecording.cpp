#include "qspiceaudio.h"
#include <QAudioDevice>
#include <QAudioSource>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QPermissions>
#include <QPointer>
#include <QtEndian>
extern "C" {
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}
namespace QtSpice {
namespace {
Q_LOGGING_CATEGORY(microphoneLog, "qtspice.microphone", QtWarningMsg)
class NativeCapture final : public AudioCapture {
    QAudioSource source;
    QIODevice *io;

  public:
    NativeCapture(const QAudioDevice &d, const QAudioFormat &f,
                  std::function<void()> failure)
        : source(d, f) {
        source.setBufferSize(f.bytesForDuration(100000));
        io = source.start();
        QObject::connect(&source, &QAudioSource::stateChanged, &source,
                         [this, failure](QtAudio::State s) {
                             if (s == QtAudio::StoppedState && failed())
                                 failure();
                         });
    }
    ~NativeCapture() override {
        source.disconnect();
        source.reset();
    }
    QIODevice *device() const override { return io; }
    bool failed() const override {
        return !io || source.error() != QtAudio::NoError;
    }
};
} // namespace
struct AudioRecording::Capture {
    std::unique_ptr<AudioCapture> source;
    SwrContext *converter = nullptr;
    QAudioFormat input;
    QByteArray partial;
    int channels = 0;
    QList<int> inputOrder;
    bool loggedInput = false, loggedOutput = false, loggedSignal = false;
    ~Capture() {
        source.reset();
        swr_free(&converter);
    }
};
AudioRecording::AudioRecording(Session *s) : owner(s) {
    factory = [](const QAudioDevice &d, const QAudioFormat &f,
                 std::function<void()> error) {
        return std::make_unique<NativeCapture>(d, f, std::move(error));
    };
    connect(&devices, &QMediaDevices::audioInputsChanged, this,
            &AudioRecording::devicesChanged);
}
AudioRecording::~AudioRecording() { detach(); }
void AudioRecording::changed() { Q_EMIT owner->microphoneChanged(); }
void AudioRecording::close() {
    ++generation;
    if (capture && capture->source && capture->source->device())
        capture->source->device()->disconnect(this);
    capture.reset();
}
void AudioRecording::devicesChanged() {
    // A pinned device must not restart because an unrelated device changed.
    bool missing = !deviceId.isEmpty();
    for (auto d : QMediaDevices::audioInputs())
        if (d.id() == deviceId)
            missing = false;
    if (enabled && requested && (deviceId.isEmpty() || (missing && capture)))
        open();
    else
        changed();
}
void AudioRecording::fail(const QString &s) {
    close();
    error = s;
    state = Session::MicrophoneState::Error;
    changed();
}
bool AudioRecording::configure(const QAudioFormat &f, int channels, int rate) {
    close();
    AVSampleFormat sample = AV_SAMPLE_FMT_NONE;
    switch (f.sampleFormat()) {
    case QAudioFormat::UInt8:
        sample = AV_SAMPLE_FMT_U8;
        break;
    case QAudioFormat::Int16:
        sample = AV_SAMPLE_FMT_S16;
        break;
    case QAudioFormat::Int32:
        sample = AV_SAMPLE_FMT_S32;
        break;
    case QAudioFormat::Float:
        sample = AV_SAMPLE_FMT_FLT;
        break;
    default:
        break;
    }
    if (!f.isValid() || sample == AV_SAMPLE_FMT_NONE || channels < 1 ||
        channels > 8 || rate < 8000 || rate > 192000 || f.channelCount() > 32 ||
        f.sampleRate() > 384000)
        return false;
    auto c = std::make_unique<Capture>();
    c->input = f;
    c->channels = channels;
    AVChannelLayout in{}, out{};
    av_channel_layout_default(&in, f.channelCount());
    av_channel_layout_default(&out, channels);
    if (f.channelConfig() != QAudioFormat::ChannelConfigUnknown) {
        const AVChannel positions[] = {AV_CHAN_NONE,
                                       AV_CHAN_FRONT_LEFT,
                                       AV_CHAN_FRONT_RIGHT,
                                       AV_CHAN_FRONT_CENTER,
                                       AV_CHAN_LOW_FREQUENCY,
                                       AV_CHAN_BACK_LEFT,
                                       AV_CHAN_BACK_RIGHT,
                                       AV_CHAN_FRONT_LEFT_OF_CENTER,
                                       AV_CHAN_FRONT_RIGHT_OF_CENTER,
                                       AV_CHAN_BACK_CENTER,
                                       AV_CHAN_SIDE_LEFT,
                                       AV_CHAN_SIDE_RIGHT,
                                       AV_CHAN_TOP_CENTER,
                                       AV_CHAN_TOP_FRONT_LEFT,
                                       AV_CHAN_TOP_FRONT_RIGHT,
                                       AV_CHAN_TOP_FRONT_CENTER,
                                       AV_CHAN_TOP_BACK_LEFT,
                                       AV_CHAN_TOP_BACK_RIGHT,
                                       AV_CHAN_TOP_BACK_CENTER,
                                       AV_CHAN_LOW_FREQUENCY_2,
                                       AV_CHAN_TOP_SIDE_LEFT,
                                       AV_CHAN_TOP_SIDE_RIGHT,
                                       AV_CHAN_BOTTOM_FRONT_CENTER,
                                       AV_CHAN_BOTTOM_FRONT_LEFT,
                                       AV_CHAN_BOTTOM_FRONT_RIGHT};
        quint64 mask = 0;
        for (int i = 1; i < QAudioFormat::NChannelPositions; ++i)
            if (f.channelOffset(QAudioFormat::AudioChannelPosition(i)) >= 0)
                mask |= quint64(1) << positions[i];
        av_channel_layout_uninit(&in);
        av_channel_layout_from_mask(&in, mask);
        for (int i = 0; i < in.nb_channels; ++i) {
            const auto ch = av_channel_layout_channel_from_index(&in, i);
            for (int j = 1; j < QAudioFormat::NChannelPositions; ++j)
                if (positions[j] == ch)
                    c->inputOrder.append(
                        f.channelOffset(QAudioFormat::AudioChannelPosition(j)));
        }
    }
    int result =
        swr_alloc_set_opts2(&c->converter, &out, AV_SAMPLE_FMT_S16, rate, &in,
                            sample, f.sampleRate(), 0, nullptr);
    av_channel_layout_uninit(&in);
    av_channel_layout_uninit(&out);
    if (result < 0 || swr_init(c->converter) < 0)
        return false;
    capture = std::move(c);
    return true;
}
void AudioRecording::data(const QByteArray &bytes) {
    if (!capture)
        return;
    const auto current = generation;
    auto &c = *capture;
    const int frame = c.input.bytesPerFrame();
    if (!c.loggedInput && !bytes.isEmpty()) {
        qCDebug(microphoneLog)
            << "capture received" << bytes.size() << "bytes" << c.input;
        c.loggedInput = true;
    }
    c.partial.append(bytes);
    // Drain in bounded chunks rather than retaining an unbounded recording.
    while (c.partial.size() >= frame) {
        int frames = int(std::min<qsizetype>(c.partial.size() / frame, 4096));
        int capacity = swr_get_out_samples(c.converter, frames);
        if (capacity < 0 || capacity > 1048576) {
            fail(QStringLiteral("Microphone conversion failed"));
            return;
        }
        QByteArray output(capacity * c.channels * 2, Qt::Uninitialized);
        auto *dst = reinterpret_cast<uint8_t *>(output.data());
        QByteArray reordered;
        if (!c.inputOrder.isEmpty()) {
            const int sampleBytes = c.input.bytesPerSample();
            reordered.resize(frames * frame);
            for (int i = 0; i < frames; ++i)
                for (int j = 0; j < c.inputOrder.size(); ++j)
                    memcpy(reordered.data() + i * frame + j * sampleBytes,
                           c.partial.constData() + i * frame +
                               c.inputOrder[j] * sampleBytes,
                           sampleBytes);
        }
        auto *src = reinterpret_cast<const uint8_t *>(
            reordered.isEmpty() ? c.partial.constData()
                                : reordered.constData());
        int count = swr_convert(c.converter, &dst, capacity, &src, frames);
        if (count < 0) {
            fail(QStringLiteral("Microphone conversion failed"));
            return;
        }
        c.partial.remove(0, frames * frame);
        output.resize(count * c.channels * 2);
        gboolean mute = FALSE;
        guint n = 0;
        guint16 *gains = nullptr;
        if (channel)
            g_object_get(channel, "mute", &mute, "nchannels", &n, "volume",
                         &gains, nullptr);
        QList<quint16> overriddenGains;
        if (controls) {
            const auto values = controls();
            mute = values.first;
            overriddenGains = values.second;
            n = overriddenGains.size();
            gains = overriddenGains.data();
        }
        int inputPeak = 0, outputPeak = 0;
        const bool trace = (!c.loggedOutput || !c.loggedSignal) &&
                           microphoneLog().isDebugEnabled();
        for (qsizetype i = 0; i < output.size(); i += 2) {
            qint16 value;
            memcpy(&value, output.constData() + i, 2);
            if (trace)
                inputPeak = std::max(inputPeak, std::abs(int(value)));
            int ch = (i / 2) % c.channels;
            value = mute ? 0
                         : qint16(value * (gains && guint(ch) < n
                                               ? gains[ch] / 65535.0
                                               : 1.0));
            if (trace)
                outputPeak = std::max(outputPeak, std::abs(int(value)));
            qToLittleEndian(value, output.data() + i);
        }
        // Keep timestamps monotonic across device switches and format changes.
        quint32 timestamp = quint32(g_get_monotonic_time() / 1000);
        if (!output.isEmpty()) {
            if (trace && (!c.loggedOutput || inputPeak > 0)) {
                qCDebug(microphoneLog)
                    << "submit" << output.size() << "bytes" << "input-peak"
                    << inputPeak << "output-peak" << outputPeak << "guest-muted"
                    << bool(mute) << "gain-channels" << n << "first-gain"
                    << (gains && n ? int(gains[0]) : -1);
                c.loggedOutput = true;
                c.loggedSignal |= inputPeak > 0;
            }
            if (send)
                send(output, timestamp);
            else if (channel && enabled && requested)
                spice_record_channel_send_data(channel, output.data(),
                                               output.size(), timestamp);
        }
        if (generation != current)
            return;
    }
}
QAudioFormat AudioRecording::captureFormat(const QAudioFormat &preferred,
                                           const QAudioFormat &requested) {
    // A device can accept stereo yet expose useful microphone channels only
    // in its native multichannel layout. Preserve all inputs before downmixing.
    return preferred.isValid() ? preferred : requested;
}
void AudioRecording::open() {
    close();
    error.clear();
    if (!enabled) {
        state = Session::MicrophoneState::Disabled;
        changed();
        return;
    }
    if (!channel) {
        state = Session::MicrophoneState::WaitingForChannel;
        changed();
        return;
    }
    if (!requested) {
        state = Session::MicrophoneState::WaitingForStream;
        changed();
        return;
    }
    QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (!deviceId.isEmpty()) {
        device = {};
        for (auto d : QMediaDevices::audioInputs())
            if (d.id() == deviceId)
                device = d;
    }
    if (device.isNull() && !inputFormat) {
        fail(QStringLiteral("Selected microphone unavailable"));
        return;
    }
    if (guestChannels < 1 || guestChannels > 8 || guestRate < 8000 ||
        guestRate > 192000) {
        fail(QStringLiteral("Unsupported guest recording format"));
        return;
    }
    if (guestFormat != SPICE_AUDIO_FMT_S16) {
        fail(QStringLiteral("Unsupported guest recording format"));
        return;
    }
    QAudioFormat format;
    format.setSampleFormat(QAudioFormat::Int16);
    format.setChannelConfig(
        QAudioFormat::defaultChannelConfigForChannelCount(guestChannels));
    format.setSampleRate(guestRate);
    if (inputFormat)
        format = inputFormat(deviceId, format);
    else
        format = captureFormat(device.preferredFormat(), format);
    if (!configure(format, guestChannels, guestRate)) {
        fail(QStringLiteral("Unsupported microphone conversion"));
        return;
    }
    const auto current = generation;
    capture->source = factory(
        device, format, [guard = QPointer<AudioRecording>(this), current] {
            if (guard)
                QMetaObject::invokeMethod(
                    guard,
                    [guard, current] {
                        if (guard && guard->generation == current)
                            guard->fail(
                                QStringLiteral("Microphone capture failed"));
                    },
                    Qt::QueuedConnection);
        });
    auto *source = capture->source.get();
    auto *io = source ? source->device() : nullptr;
    if (!io || source->failed()) {
        fail(QStringLiteral("Could not open microphone"));
        return;
    }
    auto drain = [this, io, current] {
        if (generation != current)
            return;
        // Some audio backends do not include hardware-buffered data in
        // QIODevice::bytesAvailable(). readyRead is the signal to attempt a
        // read.
        while (true) {
            const auto bytes = io->read(16384);
            if (bytes.isEmpty())
                break;
            data(bytes);
            if (generation != current)
                return;
        }
    };
    connect(io, &QIODevice::readyRead, this, drain);
    qCDebug(microphoneLog) << "capture opened" << device.description() << format
                           << "guest" << guestChannels << guestRate;
    state = Session::MicrophoneState::Recording;
    changed();
    if (generation == current)
        drain();
}
void AudioRecording::setEnabled(bool v) {
    const auto request = ++permissionRequest;
    if (enabled == v)
        return;
    if (v && !inputFormat) {
        QMicrophonePermission permission;
        const auto status = qApp->checkPermission(permission);
        if (status == Qt::PermissionStatus::Denied) {
            fail(QStringLiteral("Microphone permission denied"));
            return;
        }
        if (status == Qt::PermissionStatus::Undetermined) {
            qApp->requestPermission(
                permission, this, [this, request](const QPermission &p) {
                    if (permissionRequest != request ||
                        owner->state() != Session::State::Connected)
                        return;
                    if (p.status() == Qt::PermissionStatus::Granted) {
                        enabled = true;
                        open();
                    } else
                        fail(QStringLiteral("Microphone permission denied"));
                });
            return;
        }
    }
    enabled = v;
    open();
}
void AudioRecording::select(const QByteArray &id) {
    deviceId = id;
    open();
}
void AudioRecording::start(int f, int n, int r) {
    guestFormat = f;
    guestChannels = n;
    guestRate = r;
    requested = true;
    open();
}
void AudioRecording::stop() {
    requested = false;
    open();
}
void AudioRecording::attach(SpiceRecordChannel *c, bool connectChannel) {
    bool wasEnabled = enabled;
    detach();
    enabled = wasEnabled;
    channel = c;
    g_signal_connect(c, "record-start",
                     G_CALLBACK(+[](SpiceRecordChannel *, gint f, gint n,
                                    gint r, gpointer p) {
                         static_cast<AudioRecording *>(p)->start(f, n, r);
                     }),
                     this);
    g_signal_connect(c, "record-stop",
                     G_CALLBACK(+[](SpiceRecordChannel *, gpointer p) {
                         static_cast<AudioRecording *>(p)->stop();
                     }),
                     this);
    if (connectChannel)
        spice_channel_connect(SPICE_CHANNEL(c));
    open();
}
void AudioRecording::detach(SpiceChannel *c) {
    if (c && c != SPICE_CHANNEL(channel))
        return;
    if (channel)
        g_signal_handlers_disconnect_by_data(channel, this);
    ++permissionRequest;
    channel = nullptr;
    enabled = false;
    requested = false;
    open();
}
} // namespace QtSpice
