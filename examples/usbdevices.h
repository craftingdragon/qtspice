#pragma once
#include <QtSpice/QSpiceWidget.h>
#include <QDialog>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QLabel>
#include <QCheckBox>
#include <QTreeWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPointer>

namespace UsbDevices {
inline void add(QMainWindow *window, QtSpice::Session *session) {
    auto *action = window->menuBar()->addMenu(QStringLiteral("Devices"))->addAction(QStringLiteral("USB Devices…"));
    action->setObjectName(QStringLiteral("usbDevicesAction"));
    QObject::connect(action, &QAction::triggered, window, [window, session] {
        for (auto *widget : window->findChildren<QtSpice::Widget *>()) widget->releaseInputCapture();
        if (auto *existing = qobject_cast<QDialog *>(session->property("usbDialog").value<QObject *>())) {
            existing->show(); existing->raise(); existing->activateWindow(); return;
        }
        auto *dialog = new QDialog;
        dialog->setObjectName(QStringLiteral("usbDevicesDialog"));
        QObject::connect(session, &QObject::destroyed, dialog, [dialog] { delete dialog; });
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        session->setProperty("usbDialog", QVariant::fromValue(static_cast<QObject *>(dialog)));
        QObject::connect(dialog, &QObject::destroyed, session, [session] { session->setProperty("usbDialog", {}); });
        dialog->setWindowTitle(QStringLiteral("USB Devices"));
        auto *layout = new QVBoxLayout(dialog);
        layout->addWidget(new QLabel(QStringLiteral("Redirected devices are unavailable to the host."), dialog));
        auto *status = new QLabel(dialog); status->setWordWrap(true); layout->addWidget(status);
        auto *showInput = new QCheckBox(QStringLiteral("Show input devices"), dialog); layout->addWidget(showInput);
        auto *tree = new QTreeWidget(dialog); tree->setColumnCount(3);
        tree->setHeaderLabels({QStringLiteral("Device"), QStringLiteral("State"), QStringLiteral("Details")}); layout->addWidget(tree);
        auto *connectButton = new QPushButton(QStringLiteral("Connect"), dialog);
        auto *disconnectButton = new QPushButton(QStringLiteral("Disconnect"), dialog);
        auto *cancelButton = new QPushButton(QStringLiteral("Cancel connection"), dialog);
        auto *close = new QPushButton(QStringLiteral("Close"), dialog);
        auto *buttons = new QHBoxLayout; for (auto *button : {connectButton, disconnectButton, cancelButton, close}) buttons->addWidget(button); layout->addLayout(buttons);
        auto refresh = [=] {
            const auto *selected = tree->currentItem(); const quint64 selection = selected ? selected->data(0, Qt::UserRole).toULongLong() : 0;
            tree->clear(); bool busy = false, connecting = false;
            for (const auto &device : session->usbDevices()) {
                using S = QtSpice::UsbDeviceInfo::State;
                busy |= device.state == S::Connecting || device.state == S::Disconnecting;
                connecting |= device.state == S::Connecting;
                if (device.inputDevice && !showInput->isChecked() && device.state == S::Available) continue;
                QString state;
                switch (device.state) { case S::Available: state = QStringLiteral("Available"); break; case S::Connecting: state = QStringLiteral("Connecting…"); break; case S::Connected: state = QStringLiteral("Connected"); break; case S::Disconnecting: state = QStringLiteral("Disconnecting…"); break; }
                auto *item = new QTreeWidgetItem(tree, {device.description, state, device.reason});
                item->setData(0, Qt::UserRole, QVariant::fromValue(device.id));
                item->setData(0, Qt::UserRole + 1, device.canRedirect);
                item->setData(0, Qt::UserRole + 2, int(device.state));
                if (device.id == selection) tree->setCurrentItem(item);
            }
            dialog->setProperty("usbBusy", busy);
            cancelButton->setEnabled(connecting);
            status->setText(session->state() != QtSpice::Session::State::Connected ? QStringLiteral("Console disconnected") : !session->usbError().isEmpty() ? session->usbError() : QStringLiteral("%1 USB redirection channels available").arg(session->usbFreeChannels()));
            tree->resizeColumnToContents(0);
        };
        auto selectionChanged = [=] {
            auto *item = tree->currentItem(); const bool busy = dialog->property("usbBusy").toBool();
            const int state = item ? item->data(0, Qt::UserRole + 2).toInt() : -1;
            connectButton->setEnabled(!busy && session->state() == QtSpice::Session::State::Connected && item && item->data(0, Qt::UserRole + 1).toBool() && state == int(QtSpice::UsbDeviceInfo::State::Available));
            disconnectButton->setEnabled(!busy && state == int(QtSpice::UsbDeviceInfo::State::Connected));
        };
        QObject::connect(tree, &QTreeWidget::itemSelectionChanged, dialog, selectionChanged);
        auto update = [=] { refresh(); selectionChanged(); };
        QObject::connect(session, &QtSpice::Session::usbChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::stateChanged, dialog, update);
        QObject::connect(showInput, &QCheckBox::toggled, dialog, update);
        QObject::connect(connectButton, &QPushButton::clicked, dialog, [=] { if (tree->currentItem()) session->connectUsbDevice(tree->currentItem()->data(0, Qt::UserRole).toULongLong()); });
        QObject::connect(disconnectButton, &QPushButton::clicked, dialog, [=] { if (tree->currentItem()) session->disconnectUsbDevice(tree->currentItem()->data(0, Qt::UserRole).toULongLong()); });
        QObject::connect(cancelButton, &QPushButton::clicked, session, &QtSpice::Session::cancelUsbConnection);
        QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::close);
        update(); dialog->resize(750, 400); dialog->show();
    });
}
}
