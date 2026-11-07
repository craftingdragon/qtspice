#pragma once
#include "microphonemenu.h"
#include <QMenuBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QWidgetAction>
#include <QtSpice/QSpiceSession.h>
#include <cmath>
namespace AudioMenu {
inline void add(QMenuBar *bar, QtSpice::Session *session) {
    QSettings settings;
    session->setAudioEnabled(settings.value("audio/enabled", true).toBool());
    session->setAudioMuted(settings.value("audio/muted", false).toBool());
    session->setAudioVolume(settings.value("audio/volume", 1.0).toDouble());
    auto *menu = bar->addMenu(QStringLiteral("Audio"));
    auto *enabled = menu->addAction(QStringLiteral("Enable Audio"));
    enabled->setCheckable(true);
    auto *muted = menu->addAction(QStringLiteral("Mute"));
    muted->setCheckable(true);
    menu->addSection(QStringLiteral("Volume"));
    auto *slider = new QSlider(Qt::Horizontal, menu);
    slider->setRange(0, 100);
    slider->setAccessibleName(QStringLiteral("Volume"));
    auto *action = new QWidgetAction(menu);
    action->setDefaultWidget(slider);
    menu->addAction(action);
    auto *status = menu->addAction(QString());
    status->setEnabled(false);
    auto sync = [=] {
        QSignalBlocker b1(enabled), b2(muted), b3(slider);
        enabled->setChecked(session->audioEnabled());
        muted->setChecked(session->audioMuted());
        slider->setValue(qRound(session->audioVolume() * 100));
        using S = QtSpice::Session::AudioState;
        switch (session->audioState()) {
        case S::Disabled:
            status->setText(QStringLiteral("Audio disabled"));
            break;
        case S::WaitingForStream:
            status->setText(QStringLiteral("Waiting for guest audio"));
            break;
        case S::Playing:
            status->setText(QStringLiteral("Playback active"));
            break;
        case S::Error:
            status->setText(session->audioError());
            break;
        }
    };
    QObject::connect(session, &QtSpice::Session::audioChanged, menu, sync);
    QObject::connect(enabled, &QAction::toggled, session, [=](bool v) {
        session->setAudioEnabled(v);
        QSettings().setValue("audio/enabled", v);
    });
    QObject::connect(muted, &QAction::toggled, session, [=](bool v) {
        session->setAudioMuted(v);
        QSettings().setValue("audio/muted", v);
    });
    QObject::connect(slider, &QSlider::valueChanged, session, [=](int v) {
        session->setAudioVolume(v / 100.0);
        QSettings().setValue("audio/volume", v / 100.0);
    });
    sync();
    addMicrophoneMenu(menu, session);
}
} // namespace AudioMenu
