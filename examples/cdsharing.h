// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QtSpice/QSpiceWidget.h>
#include <QDialog>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QComboBox>
#include <QLineEdit>
#include <QFileDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
namespace CdSharing {
inline void add(QMenu *menu, QMainWindow *window, QtSpice::Session *session) {
    auto *action = menu->addAction(QStringLiteral("Share CD/DVD…"));
    action->setObjectName(QStringLiteral("shareCdAction"));
    QObject::connect(action, &QAction::triggered, window, [window, session] {
        for (auto *widget : window->findChildren<QtSpice::Widget *>()) widget->releaseInputCapture();
        if (auto *existing = qobject_cast<QDialog *>(session->property("cdDialog").value<QObject *>())) {
            existing->show(); existing->raise(); existing->activateWindow(); return;
        }
        auto *dialog = new QDialog; dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(QStringLiteral("Share CD/DVD"));
        session->setProperty("cdDialog", QVariant::fromValue(static_cast<QObject *>(dialog)));
        QObject::connect(session, &QObject::destroyed, dialog, [dialog] { delete dialog; });
        QObject::connect(dialog, &QObject::destroyed, session, [session] { session->setProperty("cdDialog", {}); });
        auto *layout = new QVBoxLayout(dialog);
        auto *kind = new QComboBox(dialog); kind->addItems({QStringLiteral("ISO Image"), QStringLiteral("Physical Drive")}); layout->addWidget(kind);
        auto *path = new QLineEdit(session->sharedCdSource(), dialog); path->setPlaceholderText(QStringLiteral("Image path or optical device such as /dev/sr0")); layout->addWidget(path);
        auto *browse = new QPushButton(QStringLiteral("Browse…"), dialog); layout->addWidget(browse);
        layout->addWidget(new QLabel(QStringLiteral("Read-only sharing uses one USB redirection channel.\nGuest eject removes the shared device. Closing this dialog leaves sharing active."), dialog));
        auto *status = new QLabel(dialog); status->setWordWrap(true); layout->addWidget(status);
        auto *share = new QPushButton(QStringLiteral("Share"), dialog);
        auto *disconnect = new QPushButton(QStringLiteral("Disconnect"), dialog);
        auto *cancel = new QPushButton(QStringLiteral("Cancel Connection"), dialog);
        auto *close = new QPushButton(QStringLiteral("Close"), dialog);
        auto *buttons = new QHBoxLayout;
        for (auto *b : {share, disconnect, cancel, close}) buttons->addWidget(b);
        layout->addLayout(buttons);
        auto refresh = [=] {
            using S = QtSpice::Session::SharedCdState;
            auto state = session->sharedCdState();
            const bool idle = state == S::Idle || state == S::Error;
            bool usbBusy = false;
            for (const auto &d : session->usbDevices()) usbBusy |= d.state == QtSpice::UsbDeviceInfo::State::Connecting || d.state == QtSpice::UsbDeviceInfo::State::Disconnecting;
            const QStringList names{QStringLiteral("Idle"),QStringLiteral("Creating…"),QStringLiteral("Connecting…"),QStringLiteral("Connected"),QStringLiteral("Disconnecting…"),QStringLiteral("Error")};
            status->setText(QStringLiteral("%1 · %2 USB channels available\n%3\n%4").arg(names[int(state)]).arg(session->usbFreeChannels()).arg(session->sharedCdSource(), session->sharedCdError()));
            share->setEnabled(idle && !usbBusy && session->state() == QtSpice::Session::State::Connected && session->usbFreeChannels() > 0);
            disconnect->setEnabled(state == S::Connected && !usbBusy);
            cancel->setEnabled(state == S::Creating || state == S::Connecting);
            path->setEnabled(idle); browse->setEnabled(idle); kind->setEnabled(idle);
        };
        QObject::connect(browse, &QPushButton::clicked, dialog, [=] {
            const auto selected = QFileDialog::getOpenFileName(dialog, kind->currentIndex() ? QStringLiteral("Select optical device") : QStringLiteral("Select ISO image"), kind->currentIndex() ? QStringLiteral("/dev") : QString(), kind->currentIndex() ? QString() : QStringLiteral("ISO images (*.iso);;All files (*)"));
            if (!selected.isEmpty()) path->setText(selected);
        });
        QObject::connect(share, &QPushButton::clicked, dialog, [=] { QString error; if (!session->shareCd(path->text(), &error)) status->setText(error); });
        QObject::connect(disconnect, &QPushButton::clicked, session, &QtSpice::Session::disconnectSharedCd);
        QObject::connect(cancel, &QPushButton::clicked, session, &QtSpice::Session::disconnectSharedCd);
        QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::close);
        QObject::connect(session, &QtSpice::Session::sharedCdChanged, dialog, refresh);
        QObject::connect(session, &QtSpice::Session::usbChanged, dialog, refresh);
        QObject::connect(session, &QtSpice::Session::stateChanged, dialog, refresh);
        refresh(); dialog->resize(640, 280); dialog->show();
    });
}
}
