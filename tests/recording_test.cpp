// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../examples/audiomenu.h"
#include "../src/core/qspiceaudio.h"
#include "../src/core/qspicesession.h"
#include <QBuffer>
#include <QtEndian>
#include <QtTest>
using namespace QtSpice;
namespace QtSpice {
struct SessionTestAccess {
    static AudioRecording &recording(Session &s) {
        return s.worker_->microphone;
    }
    static void connected(Session &s) {
        s.state_ = Session::State::Connected;
        Q_EMIT s.stateChanged(s.state_);
    }
};
} // namespace QtSpice
struct FakeCapture : AudioCapture {
    QBuffer buffer;
    FakeCapture() { buffer.open(QIODevice::ReadOnly); }
    QIODevice *device() const override {
        return const_cast<QBuffer *>(&buffer);
    }
    bool failed() const override { return false; }
    void feed(const QByteArray &b) {
        buffer.buffer() = b;
        buffer.seek(0);
        Q_EMIT buffer.readyRead();
    }
};
// Models audio devices whose bytesAvailable() excludes native capture buffers.
struct SequentialCapture : AudioCapture {
    struct Device : QIODevice {
        QByteArray pending;
        Device() { open(QIODevice::ReadOnly | QIODevice::Unbuffered); }
        bool isSequential() const override { return true; }
        qint64 bytesAvailable() const override { return 0; }
        qint64 readData(char *p, qint64 n) override {
            n = std::min<qint64>(n, pending.size());
            memcpy(p, pending.constData(), n);
            pending.remove(0, n);
            return n;
        }
        qint64 writeData(const char *, qint64) override { return -1; }
    } io;
    QIODevice *device() const override { return const_cast<Device *>(&io); }
    bool failed() const override { return false; }
};
class RecordingTest : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDirectory;
  private Q_SLOTS:
    void initTestCase() {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           settingsDirectory.path());
        QCoreApplication::setOrganizationName("QtSpiceTests");
        QCoreApplication::setApplicationName("RecordingTest");
    }
    void lifecycle() {
        Session s;
        AudioRecording r(&s);
        QCOMPARE(r.state, Session::MicrophoneState::Disabled);
        r.start(SPICE_AUDIO_FMT_S16, 2, 48000);
        QCOMPARE(r.state, Session::MicrophoneState::Disabled);
        r.setEnabled(true);
        QCOMPARE(r.state, Session::MicrophoneState::WaitingForChannel);
        r.detach();
        QVERIFY(!r.enabled);
        s.setMicrophoneEnabled(true);
        QVERIFY(!s.microphoneEnabled());
    }
    void formats_data() {
        QTest::addColumn<int>("sample");
        for (int f : {int(QAudioFormat::UInt8), int(QAudioFormat::Int16),
                      int(QAudioFormat::Int32), int(QAudioFormat::Float)})
            QTest::newRow(qPrintable(QString::number(f))) << f;
    }
    void formats() {
        QFETCH(int, sample);
        Session s;
        AudioRecording r(&s);
        QAudioFormat f;
        f.setSampleFormat(QAudioFormat::SampleFormat(sample));
        f.setSampleRate(48000);
        f.setChannelCount(1);
        QVERIFY(r.configure(f, 2, 44100));
        QByteArray sent;
        quint32 last = 0;
        r.send = [&](const QByteArray &b, quint32 t) {
            QVERIFY(t >= last);
            last = t;
            QCOMPARE(b.size() % 4, 0);
            sent += b;
        };
        QByteArray pcm(4800 * f.bytesPerFrame(), 0);
        if (sample == QAudioFormat::UInt8)
            pcm.fill(char(128));
        r.data(pcm.left(1));
        QVERIFY(sent.isEmpty());
        r.data(pcm.mid(1));
        QVERIFY(sent.size() > 4000 * 4);
        QVERIFY(sent.size() <= 4410 * 4);
        for (char b : sent)
            QCOMPARE(b, char(0));
        r.detach();
        auto n = sent.size();
        r.data(pcm);
        QCOMPARE(sent.size(), n);
        QVERIFY(!r.configure({}, 2, 44100));
        QVERIFY(!r.configure(f, 0, 44100));
    }
    void conversionAndReset() {
        Session s;
        AudioRecording r(&s);
        QAudioFormat f;
        f.setSampleRate(48000);
        f.setChannelCount(1);
        f.setSampleFormat(QAudioFormat::Float);
        QVERIFY(r.configure(f, 2, 48000));
        QByteArray b;
        r.send = [&](const QByteArray &v, quint32) { b += v; };
        float samples[100];
        std::fill_n(samples, 100, 0.5f);
        r.data(QByteArray(reinterpret_cast<char *>(samples), sizeof(samples)));
        QVERIFY(!b.isEmpty());
        for (int i = 0; i < b.size(); i += 4)
            QCOMPARE(qFromLittleEndian<qint16>(b.constData() + i),
                     qFromLittleEndian<qint16>(b.constData() + i + 2));
        QVERIFY(qFromLittleEndian<qint16>(b.constData()) > 10000);
        r.configure(f, 1, 48000);
        b.clear();
        r.data(QByteArray(3, 0));
        QVERIFY(b.isEmpty());
        r.configure(f, 1, 48000);
        r.data(QByteArray(1, 0));
        QVERIFY(b.isEmpty());
    }
    void clippingAndDownmix() {
        Session s;
        AudioRecording r(&s);
        QAudioFormat f;
        f.setSampleFormat(QAudioFormat::Float);
        f.setSampleRate(48000);
        f.setChannelConfig(QAudioFormat::ChannelConfigStereo);
        QVERIFY(r.configure(f, 1, 48000));
        QByteArray sent;
        r.send = [&](const QByteArray &b, quint32) { sent += b; };
        float frames[200];
        std::fill_n(frames, 200, 2.0f);
        r.data(QByteArray(reinterpret_cast<char *>(frames), sizeof(frames)));
        QVERIFY(!sent.isEmpty());
        for (int i = 0; i < sent.size(); i += 2)
            QCOMPARE(qFromLittleEndian<qint16>(sent.constData() + i),
                     qint16(32767));
        QVERIFY(r.configure(f, 1, 48000));
        sent.clear();
        for (int i = 0; i < 200; i += 2) {
            frames[i] = 0.5f;
            frames[i + 1] = -0.5f;
        }
        r.data(QByteArray(reinterpret_cast<char *>(frames), sizeof(frames)));
        for (char b : sent)
            QCOMPARE(b, char(0));
    }
    void nativeMultichannelMicrophone() {
        Session s;
        AudioRecording r(&s);
        QAudioFormat requested;
        requested.setSampleRate(48000);
        requested.setChannelConfig(QAudioFormat::ChannelConfigStereo);
        requested.setSampleFormat(QAudioFormat::Int16);
        QAudioFormat preferred;
        preferred.setSampleRate(48000);
        preferred.setChannelConfig(QAudioFormat::channelConfig(
            QAudioFormat::FrontLeft, QAudioFormat::FrontRight,
            QAudioFormat::BackLeft, QAudioFormat::BackRight));
        preferred.setSampleFormat(QAudioFormat::Int32);
        const auto selected =
            AudioRecording::captureFormat(preferred, requested);
        QCOMPARE(selected, preferred);
        QVERIFY(r.configure(selected, 2, 48000));
        QByteArray pcm(100 * selected.bytesPerFrame(), 0);
        // Front inputs are silent; the useful microphone pair is at the rear.
        for (int frame = 0; frame < 100; ++frame)
            for (int c : {2, 3}) {
                const qint32 sample = 1 << 29;
                memcpy(pcm.data() + (frame * 4 + c) * 4, &sample, 4);
            }
        QByteArray sent;
        r.send = [&](const QByteArray &b, quint32) { sent += b; };
        r.data(pcm);
        QVERIFY(!sent.isEmpty());
        for (int i = 0; i < sent.size(); i += 4) {
            QVERIFY(qFromLittleEndian<qint16>(sent.constData() + i) > 1000);
            QVERIFY(qFromLittleEndian<qint16>(sent.constData() + i + 2) > 1000);
        }
        QCOMPARE(AudioRecording::captureFormat({}, requested), requested);
    }
    void channelAndDevices() {
        Session session;
        auto &r = SessionTestAccess::recording(session);
        SessionTestAccess::connected(session);
        FakeCapture *input = nullptr;
        int opens = 0;
        bool missing = false;
        r.inputFormat = [&](const QByteArray &, const QAudioFormat &f) {
            return missing ? QAudioFormat() : f;
        };
        std::function<void()> oldError;
        r.factory = [&](const QAudioDevice &, const QAudioFormat &,
                        std::function<void()> error) {
            ++opens;
            oldError = error;
            auto p = std::make_unique<FakeCapture>();
            input = p.get();
            return p;
        };
        auto *native = spice_session_new();
        auto *channel = SPICE_RECORD_CHANNEL(
            spice_channel_new(native, SPICE_CHANNEL_RECORD, 0));
        r.attach(channel, false);
        session.setMicrophoneEnabled(true);
        QCOMPARE(opens, 0);
        g_signal_emit_by_name(channel, "record-start", SPICE_AUDIO_FMT_S16, 1,
                              48000);
        QCOMPARE(r.state, Session::MicrophoneState::Recording);
        QCOMPARE(opens, 1);
        QByteArray sent;
        r.send = [&](const QByteArray &b, quint32) { sent += b; };
        QByteArray pcm(200, 0);
        for (int i = 0; i < 200; i += 2)
            qToLittleEndian<qint16>(10000, pcm.data() + i);
        input->feed(pcm);
        QVERIFY(!sent.isEmpty());
        QCOMPARE(qFromLittleEndian<qint16>(sent.constData()), qint16(10000));
        r.controls = [] { return qMakePair(true, QList<quint16>{}); };
        sent.clear();
        input->feed(pcm);
        for (char b : sent)
            QCOMPARE(b, char(0));
        r.controls = [] { return qMakePair(false, QList<quint16>{32768}); };
        sent.clear();
        input->feed(pcm);
        QVERIFY(qFromLittleEndian<qint16>(sent.constData()) >= 4999);
        QVERIFY(qFromLittleEndian<qint16>(sent.constData()) <= 5001);
        auto stale = oldError;
        g_signal_emit_by_name(channel, "record-stop");
        QVERIFY(r.enabled);
        QCOMPARE(r.state, Session::MicrophoneState::WaitingForStream);
        stale();
        QCoreApplication::processEvents();
        QCOMPARE(r.state, Session::MicrophoneState::WaitingForStream);
        g_signal_emit_by_name(channel, "record-start", SPICE_AUDIO_FMT_S16, 1,
                              48000);
        QCOMPARE(opens, 2);
        missing = true;
        session.setMicrophoneDevice("removed");
        QCOMPARE(r.state, Session::MicrophoneState::Error);
        missing = false;
        session.setMicrophoneDevice({});
        QCOMPARE(r.state, Session::MicrophoneState::Recording);
        oldError();
        QCoreApplication::processEvents();
        QCOMPARE(r.state, Session::MicrophoneState::Error);
        r.detach();
        QVERIFY(!session.microphoneEnabled());
        r.attach(channel, false);
        g_signal_emit_by_name(channel, "record-start", SPICE_AUDIO_FMT_S16, 1,
                              48000);
        QCOMPARE(r.state, Session::MicrophoneState::Disabled);
        r.detach();
        g_object_unref(channel);
        g_object_unref(native);
    }
    void sharedMenus() {
        Session s;
        SessionTestAccess::connected(s);
        auto &r = SessionTestAccess::recording(s);
        r.inputFormat = [](const QByteArray &, const QAudioFormat &f) {
            return f;
        };
        QMainWindow a, b;
        AudioMenu::add(a.menuBar(), &s);
        AudioMenu::add(b.menuBar(), &s);
        auto *first = a.findChild<QAction *>("enableMicrophoneAction");
        auto *second = b.findChild<QAction *>("enableMicrophoneAction");
        QVERIFY(first && second);
        first->trigger();
        QVERIFY(first->isChecked());
        QVERIFY(second->isChecked());
        QVERIFY(!QSettings().contains("audio/microphoneEnabled"));
        s.setMicrophoneDevice("pinned");
        QMainWindow c;
        AudioMenu::add(c.menuBar(), &s);
        QCOMPARE(s.microphoneDevice(), QByteArray("pinned"));
        QVERIFY(s.microphoneEnabled());
        second->trigger();
        QVERIFY(!first->isChecked());
        QVERIFY(!s.microphoneEnabled());
        r.detach();
        QVERIFY(!c.findChild<QAction *>("enableMicrophoneAction")->isChecked());
    }
    void lateFailureAndReentrantStop() {
        Session s;
        std::function<void()> error;
        {
            AudioRecording r(&s);
            r.inputFormat = [](const QByteArray &, const QAudioFormat &f) {
                return f;
            };
            r.factory = [&](const QAudioDevice &, const QAudioFormat &,
                            std::function<void()> e) {
                error = e;
                return std::make_unique<FakeCapture>();
            };
            auto *native = spice_session_new();
            auto *channel = SPICE_RECORD_CHANNEL(
                spice_channel_new(native, SPICE_CHANNEL_RECORD, 0));
            r.attach(channel, false);
            r.setEnabled(true);
            r.start(SPICE_AUDIO_FMT_S16, 1, 48000);
            QVERIFY(bool(error));
            r.detach();
            g_object_unref(channel);
            g_object_unref(native);
            QAudioFormat f;
            f.setSampleFormat(QAudioFormat::Int16);
            f.setSampleRate(48000);
            f.setChannelCount(1);
            QVERIFY(r.configure(f, 1, 48000));
            int sends = 0;
            r.send = [&](const QByteArray &, quint32) {
                ++sends;
                r.detach();
            };
            r.data(QByteArray(32000, 0));
            QCOMPARE(sends, 1);
        }
        error();
        QCoreApplication::processEvents();
    }
    void sequentialCaptureWithoutAvailableBytes() {
        Session session;
        AudioRecording r(&session);
        r.inputFormat = [](const QByteArray &, const QAudioFormat &f) {
            return f;
        };
        SequentialCapture *input = nullptr;
        r.factory = [&](const QAudioDevice &, const QAudioFormat &,
                        std::function<void()>) {
            auto p = std::make_unique<SequentialCapture>();
            input = p.get();
            return p;
        };
        auto *native = spice_session_new();
        auto *channel = SPICE_RECORD_CHANNEL(
            spice_channel_new(native, SPICE_CHANNEL_RECORD, 0));
        r.attach(channel, false);
        r.setEnabled(true);
        r.start(SPICE_AUDIO_FMT_S16, 1, 48000);
        QByteArray sent;
        r.send = [&](const QByteArray &b, quint32) { sent += b; };
        QByteArray pcm(20000, 0);
        for (int i = 0; i < pcm.size(); i += 2)
            qToLittleEndian<qint16>(1234, pcm.data() + i);
        input->io.pending = pcm;
        QCOMPARE(input->io.bytesAvailable(), qint64(0));
        Q_EMIT input->io.readyRead();
        QCOMPARE(sent, pcm);
        QVERIFY(input->io.pending.isEmpty());
        r.detach();
        g_object_unref(channel);
        g_object_unref(native);
    }
    void labels() {
        QMainWindow w;
        Session s;
        AudioMenu::add(w.menuBar(), &s);
        w.show();
        SessionTestAccess::connected(s);
        auto &r = SessionTestAccess::recording(s);
        r.state = Session::MicrophoneState::Recording;
        Q_EMIT s.microphoneChanged();
        auto *label = w.findChild<QLabel *>("microphoneFullscreenIndicator");
        QVERIFY(label);
        QVERIFY(label->isHidden());
        QVERIFY(label->testAttribute(Qt::WA_TransparentForMouseEvents));
        auto *status = new QLabel("Existing status", &w);
        w.statusBar()->addPermanentWidget(status);
        w.showFullScreen();
        QCoreApplication::processEvents();
        QCOMPARE(status->text(), QString("Existing status"));
        QVERIFY(label->isVisible());
        auto before = label->pos();
        w.resize(900, 600);
        QCoreApplication::processEvents();
        QVERIFY(label->x() >= 0);
        QVERIFY(label->y() >= 0);
        w.showNormal();
        QCoreApplication::processEvents();
        QVERIFY(label->isHidden());
        r.state = Session::MicrophoneState::Disabled;
        Q_EMIT s.microphoneChanged();
        QVERIFY(w.findChild<QLabel *>("microphoneStatus")->isHidden());
        QCOMPARE(
            w.findChildren<QLabel *>("microphoneFullscreenIndicator").size(),
            1);
    }
};
QTEST_MAIN(RecordingTest)
#include "recording_test.moc"
