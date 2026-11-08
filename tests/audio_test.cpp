// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../examples/audiomenu.h"
#include "../src/core/qspiceaudio.h"
#include <QtTest>
using namespace QtSpice;
struct Output : AudioOutput {
    QByteArray bytes;
    qint64 space = 1000000, quantum = 1000000;
    bool broken = false;
    qint64 freeBytes() const override { return space; }
    qint64 queuedBytes() const override { return 0; }
    qint64 write(const char *p, qint64 n) override {
        n = std::min(n, quantum);
        bytes.append(p, n);
        return n;
    }
    bool failed() const override { return broken; }
};
class AudioTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void buffering() {
        Session session;
        Output *out = nullptr;
        AudioPlayback audio(&session, [&](const QAudioFormat &, int) {
            auto p = std::make_unique<Output>();
            out = p.get();
            return p;
        });
        audio.start(SPICE_AUDIO_FMT_S16, 2, 48000);
        QCOMPARE(audio.state, Session::AudioState::Playing);
        out->quantum = 3;
        QByteArray pcm(400, '\x12');
        audio.data(pcm.data(), pcm.size());
        while (audio.bufferedBytes())
            audio.pump();
        QCOMPARE(out->bytes, pcm);
        out->space = 0;
        QByteArray huge(384000, '\x22');
        audio.data(huge.data(), huge.size());
        QCOMPARE(audio.bufferedBytes(), 192000);
        QCOMPARE(audio.delay(), 1000u);
        audio.setEnabled(false);
        QCOMPARE(audio.bufferedBytes(), 0);
        QCOMPARE(audio.state, Session::AudioState::Disabled);
        audio.setEnabled(true);
        QCOMPARE(audio.state, Session::AudioState::Playing);
        out->broken = true;
        audio.pump();
        QCOMPARE(audio.state, Session::AudioState::Error);
        audio.stop();
        QCOMPARE(audio.state, Session::AudioState::WaitingForStream);
    }
    void muteAndFormat() {
        Session session;
        Output *out = nullptr;
        AudioPlayback audio(&session, [&](const QAudioFormat &, int) {
            auto p = std::make_unique<Output>();
            out = p.get();
            return p;
        });
        audio.start(SPICE_AUDIO_FMT_S16, 1, 44100);
        audio.muted = true;
        QByteArray pcm(40, '\x55');
        audio.data(pcm.data(), pcm.size());
        QCOMPARE(out->bytes, QByteArray(40, 0));
        audio.start(999, 2, 48000);
        QCOMPARE(audio.state, Session::AudioState::Error);
        audio.start(SPICE_AUDIO_FMT_S16, 2, 48000);
        audio.data(pcm.data(), 3);
        QCOMPARE(audio.state, Session::AudioState::Error);
    }
    void unavailableOutput() {
        Session session;
        AudioPlayback audio(
            &session,
            [](const QAudioFormat &, int) -> std::unique_ptr<AudioOutput> {
                return {};
            });
        audio.start(SPICE_AUDIO_FMT_S16, 2, 48000);
        QCOMPARE(audio.state, Session::AudioState::Error);
        QVERIFY(!audio.error.isEmpty());
        audio.setEnabled(false);
        QCOMPARE(audio.state, Session::AudioState::Disabled);
        audio.setEnabled(true);
        QCOMPARE(audio.state, Session::AudioState::Error);
        audio.detach();
        QCOMPARE(audio.state, Session::AudioState::WaitingForStream);
    }
    void controls() {
        QCoreApplication::setOrganizationName("QtSpiceTest");
        QCoreApplication::setApplicationName("AudioTest");
        QTemporaryDir dir;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           dir.path());
        Session s;
        QVERIFY(s.audioEnabled());
        QVERIFY(!s.audioMuted());
        QCOMPARE(s.audioVolume(), 1.0);
        QMenuBar bar;
        AudioMenu::add(&bar, &s);
        auto *menu = bar.actions().first()->menu();
        menu->actions()[1]->trigger();
        QVERIFY(s.audioMuted());
        Session second;
        QMenuBar bar2;
        AudioMenu::add(&bar2, &second);
        QVERIFY(second.audioMuted());
        s.setAudioVolume(2);
        QCOMPARE(s.audioVolume(), 1.0);
        s.setAudioEnabled(false);
        QVERIFY(!s.audioEnabled());
        QVERIFY(second.audioEnabled());
    }
};
QTEST_MAIN(AudioTest)
#include "audio_test.moc"
