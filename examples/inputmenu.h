#pragma once
#include <QtSpice/QSpiceWidget.h>
#include <QMenuBar>
#include <QStatusBar>
#include <QLabel>
#include <QSettings>
inline void addInputMenu(QMenuBar *bar, QStatusBar *status, QtSpice::Widget *widget) {
    auto *action = bar->addMenu(QStringLiteral("Input"))->addAction(QStringLiteral("Capture Keyboard"));
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
