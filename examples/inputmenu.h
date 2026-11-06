#pragma once
#include <QtSpice/QSpiceWidget.h>
#include <QtSpice/QSpiceSession.h>
#include <QMenuBar>
#include <QStatusBar>
#include <QLabel>
#include <QSettings>
inline void addInputMenu(QMenuBar *bar, QStatusBar *status, QtSpice::Widget *widget, QtSpice::Session *session = nullptr) {
    auto *menu = bar->addMenu(QStringLiteral("Input"));
    auto *send = menu->addAction(QStringLiteral("Send Ctrl+Alt+Delete"));
    send->setObjectName(QStringLiteral("sendCtrlAltDelete"));
    send->setEnabled(session && session->inputReady());
    if (session) {
        QObject::connect(send, &QAction::triggered, session, &QtSpice::Session::sendCtrlAltDelete);
        QObject::connect(session, &QtSpice::Session::inputReadyChanged, send, &QAction::setEnabled);
        QObject::connect(session, &QObject::destroyed, send, [send] { send->setEnabled(false); });
    }
    auto *sync = menu->addAction(QStringLiteral("Synchronize Lock Keys"));
    sync->setObjectName(QStringLiteral("synchronizeLockKeys"));
    sync->setCheckable(true);
    auto updateSync = [sync, widget] {
        sync->setToolTip(widget->lockSynchronizationAvailable()
            ? QStringLiteral("Match guest Caps Lock, Num Lock and Scroll Lock to the local keyboard while the console has focus.")
            : QStringLiteral("Native lock-state synchronization is unavailable or awaiting the local keyboard on this platform."));
    };
    QObject::connect(widget, &QtSpice::Widget::lockSynchronizationAvailableChanged, sync, updateSync);
    QObject::connect(sync, &QAction::toggled, widget, [widget](bool enabled) {
        widget->setSynchronizeLockKeys(enabled);
        QSettings().setValue(QStringLiteral("input/synchronizeLockKeys"), enabled);
    });
    QObject::connect(widget, &QtSpice::Widget::synchronizeLockKeysChanged, sync, &QAction::setChecked);
    sync->setChecked(QSettings().value(QStringLiteral("input/synchronizeLockKeys"), true).toBool());
    widget->setSynchronizeLockKeys(sync->isChecked());
    updateSync();
    menu->addSeparator();
    auto *action = menu->addAction(QStringLiteral("Capture Keyboard"));
    action->setObjectName(QStringLiteral("captureKeyboard"));
    action->setCheckable(true);
    auto *label = new QLabel(status);
    status->addPermanentWidget(label);
    auto refresh = [widget, label] {
        using S = QtSpice::Widget::KeyboardCaptureState;
        QString text;
        switch (widget->keyboardCaptureState()) {
        case S::Disabled: text = QStringLiteral("Keyboard capture off"); break;
        case S::WaitingForFocus: text = QStringLiteral("Keyboard capture waiting for console"); break;
        case S::Pending: text = QStringLiteral("Keyboard capture awaiting compositor"); break;
        case S::Active: text = QStringLiteral("Keyboard captured · Ctrl+Alt+G releases input"); break;
        case S::Suspended: text = QStringLiteral("Keyboard released · Click console to capture"); break;
        case S::Unavailable: text = QStringLiteral("Keyboard capture unavailable"); break;
        case S::Failed: text = QStringLiteral("Keyboard capture failed or revoked"); break;
        }
        label->setText(text);
        label->setToolTip(QStringLiteral("XWayland capture depends on compositor policy. Ctrl+Alt+G releases keyboard and mouse."));
    };
    QObject::connect(widget, &QtSpice::Widget::keyboardCaptureStateChanged, label, refresh);
    QObject::connect(action, &QAction::toggled, widget, [widget](bool enabled) {
        widget->setKeyboardCaptureEnabled(enabled);
        QSettings().setValue(QStringLiteral("input/captureKeyboard"), enabled);
    });
    QObject::connect(widget, &QtSpice::Widget::keyboardCaptureEnabledChanged, action, &QAction::setChecked);
    action->setChecked(QSettings().value(QStringLiteral("input/captureKeyboard"), false).toBool());
    refresh();
}
