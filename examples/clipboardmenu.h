// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QtSpice/QSpiceSession.h>
#include <QActionGroup>
#include <QMenu>
#include <QSettings>
namespace ClipboardMenu {
using Mode = QtSpice::Session::ClipboardMode;
inline QString key(Mode mode) {
    switch (mode) {
    case Mode::HostToGuest: return QStringLiteral("host-to-guest");
    case Mode::GuestToHost: return QStringLiteral("guest-to-host");
    case Mode::Disabled: return QStringLiteral("disabled");
    default: return QStringLiteral("bidirectional");
    }
}
inline void initialize(QtSpice::Session *session) {
    const auto value = QSettings().value(QStringLiteral("input/clipboardMode")).toString();
    session->setClipboardMode(value == "host-to-guest" ? Mode::HostToGuest : value == "guest-to-host" ? Mode::GuestToHost : value == "disabled" ? Mode::Disabled : Mode::Bidirectional);
}
inline void add(QMenu *input, QtSpice::Session *session) {
    auto *menu = input->addMenu(QStringLiteral("Clipboard"));
    menu->setObjectName(QStringLiteral("clipboardMenu"));
    auto *group = new QActionGroup(menu);
    const QStringList names{QStringLiteral("Bidirectional"), QStringLiteral("Host → Guest Only"), QStringLiteral("Guest → Host Only"), QStringLiteral("Disabled")};
    for (int i = 0; i < names.size(); ++i) {
        const auto mode = static_cast<Mode>(i);
        auto *action = menu->addAction(names[i]); action->setCheckable(true); group->addAction(action);
        action->setChecked(session->clipboardMode() == mode);
        QObject::connect(action, &QAction::triggered, session, [session, mode] {
            session->setClipboardMode(mode);
            QSettings().setValue(QStringLiteral("input/clipboardMode"), key(mode));
        });
        QObject::connect(session, &QtSpice::Session::clipboardModeChanged, action, [action, mode](Mode current) { action->setChecked(current == mode); });
    }
}
}
