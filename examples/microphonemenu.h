// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QActionGroup>
#include <QEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QtSpice/QSpiceSession.h>
class MicrophoneIndicator final : public QObject {
  public:
    MicrophoneIndicator(QMainWindow *w, QtSpice::Session *s)
        : QObject(w), window(w), session(s) {
        status = new QLabel(w);
        status->setObjectName("microphoneStatus");
        w->statusBar()->addPermanentWidget(status);
        overlay = new QLabel(QStringLiteral("Microphone active"), w);
        overlay->setObjectName("microphoneFullscreenIndicator");
        overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
        overlay->setFocusPolicy(Qt::NoFocus);
        overlay->setStyleSheet(
            QStringLiteral("QLabel { color: white; background-color: #252525; "
                           "padding: 4px; }"));
        w->installEventFilter(this);
        connect(s, &QtSpice::Session::microphoneChanged, this,
                [this] { refresh(); });
        refresh();
    }
    void refresh() {
        bool active = session->microphoneState() ==
                      QtSpice::Session::MicrophoneState::Recording;
        status->setText(active ? QStringLiteral("Microphone active")
                               : QString());
        status->setVisible(active);
        overlay->adjustSize();
        overlay->move(window->width() - overlay->width() - 8,
                      window->height() - overlay->height() - 8);
        overlay->setVisible(active && window->isFullScreen());
        if (overlay->isVisible())
            overlay->raise();
    }

  protected:
    bool eventFilter(QObject *, QEvent *e) override {
        if (e->type() == QEvent::Resize ||
            e->type() == QEvent::WindowStateChange)
            refresh();
        return false;
    }

  private:
    QMainWindow *window;
    QtSpice::Session *session;
    QLabel *status, *overlay;
};
inline void addMicrophoneMenu(QMenu *menu, QtSpice::Session *session) {
    if (!session->property("microphoneSettingsLoaded").toBool()) {
        session->setProperty("microphoneSettingsLoaded", true);
        session->setMicrophoneDevice(
            QSettings().value("audio/microphoneDevice").toByteArray());
    }
    menu->addSeparator();
    auto *enabled = menu->addAction(QStringLiteral("Enable Microphone"));
    enabled->setObjectName("enableMicrophoneAction");
    enabled->setCheckable(true);
    auto *inputs = menu->addMenu(QStringLiteral("Input Device"));
    auto *status = menu->addAction(QString());
    status->setEnabled(false);
    auto sync = [=] {
        QSignalBlocker blocker(enabled);
        enabled->setChecked(session->microphoneEnabled());
        enabled->setEnabled(session->state() ==
                            QtSpice::Session::State::Connected);
        using S = QtSpice::Session::MicrophoneState;
        switch (session->microphoneState()) {
        case S::Disabled:
            status->setText("Microphone disabled");
            break;
        case S::WaitingForChannel:
            status->setText("Waiting for recording channel");
            break;
        case S::WaitingForStream:
            status->setText("Waiting for guest recording");
            break;
        case S::Recording:
            status->setText("Microphone active");
            break;
        case S::Error:
            status->setText(session->microphoneError());
            break;
        }
    };
    QObject::connect(inputs, &QMenu::aboutToShow, inputs, [=] {
        inputs->clear();
        for (auto *old : inputs->findChildren<QActionGroup *>(
                 QString(), Qt::FindDirectChildrenOnly))
            delete old;
        auto *group = new QActionGroup(inputs);
        group->setExclusive(true);
        auto add = [=](QString name, QByteArray id, bool available = true) {
            auto *a = inputs->addAction(name);
            group->addAction(a);
            a->setCheckable(true);
            a->setChecked(id == session->microphoneDevice());
            a->setEnabled(available);
            QObject::connect(a, &QAction::triggered, inputs, [=] {
                session->setMicrophoneDevice(id);
                QSettings().setValue("audio/microphoneDevice", id);
            });
        };
        add(QStringLiteral("System default"), {});
        bool found = session->microphoneDevice().isEmpty();
        for (auto d : session->audioInputDevices()) {
            add(d.description, d.id);
            found |= d.id == session->microphoneDevice();
        }
        if (!found)
            add(QStringLiteral("Selected microphone unavailable"),
                session->microphoneDevice(), false);
    });
    QObject::connect(enabled, &QAction::toggled, session,
                     &QtSpice::Session::setMicrophoneEnabled);
    QObject::connect(session, &QtSpice::Session::microphoneChanged, menu, sync);
    QObject::connect(session, &QtSpice::Session::stateChanged, menu, sync);
    sync();
    if (auto *w = qobject_cast<QMainWindow *>(menu->parentWidget()->window()))
        new MicrophoneIndicator(w, session);
}
