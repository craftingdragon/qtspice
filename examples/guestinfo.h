#pragma once
#include "compressionmenu.h"
#include <QMainWindow>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QClipboard>
#include <QApplication>
#include <QScrollBar>
#include <QtSpice/QSpiceWidget.h>
#include <QMetaEnum>
#include <algorithm>

namespace GuestInfo {
inline QString enumName(const QMetaEnum &meta, int value) {
    const char *name = meta.valueToKey(value);
    return name ? QString::fromLatin1(name) : QStringLiteral("Unknown");
}
template<typename T> inline QString stateName(T value) { return enumName(QMetaEnum::fromType<T>(), int(value)); }
inline QString geometry(const QtSpice::MonitorInfo &m) {
    return m.enabled ? QStringLiteral("%1×%2 at %3,%4").arg(m.geometry.width()).arg(m.geometry.height()).arg(m.geometry.x()).arg(m.geometry.y()) : QStringLiteral("Disabled");
}
inline QString monitorDetails(const QList<QtSpice::MonitorInfo> &actual,
                              const QList<QtSpice::MonitorInfo> &requested,
                              const QString &status) {
    QString text = QStringLiteral("Resize status: %1\n").arg(status.isEmpty() ? QStringLiteral("No outstanding resize warning") : status);
    QMap<QtSpice::MonitorId, QtSpice::MonitorInfo> observed, targets;
    for (const auto &m : actual) observed.insert(m.id, m);
    for (const auto &m : requested) targets.insert(m.id, m);
    auto ids = observed;
    for (const auto &m : requested) ids.insert(m.id, m);
    for (auto it = ids.cbegin(); it != ids.cend(); ++it) {
        const auto id = it.key();
        text += QStringLiteral("\nMonitor %1:%2 — Actual: %3").arg(id.channelId).arg(id.monitorId)
            .arg(observed.contains(id) ? geometry(observed[id]) : QStringLiteral("Unavailable"));
        if (targets.contains(id) && (!observed.contains(id) || geometry(targets[id]) != geometry(observed[id])))
            text += QStringLiteral("; last requested: %1").arg(geometry(targets[id]));
    }
    if (ids.isEmpty()) text += QStringLiteral("\nNo monitors available");
    return text;
}
inline QStringList sections(QtSpice::Session *s) {
    auto d = s->diagnostics();
    auto yes = [](bool v) { return v ? QStringLiteral("Yes") : QStringLiteral("No"); };
    auto advertised = [](bool v) { return v ? QStringLiteral("Advertised") : QStringLiteral("Not advertised"); };
    QString overview = QStringLiteral("Name: %1\nUUID: %2\nConnection: %3\nMouse mode: %4\nInput ready: %5\nGuest agent connected: %6\nClipboard by demand: %7\nMonitor configuration: %8\nMonitor positioning: %9\nFile transfer disabled by guest: %10\nCompression preference: %11\nCompression request: %12")
        .arg(s->guestName().isEmpty() ? QStringLiteral("Not available") : s->guestName(), s->guestUuid().isEmpty() ? QStringLiteral("Not available") : s->guestUuid(), stateName(s->state()), stateName(s->mouseMode()), yes(s->inputReady()), yes(d.agentConnected), advertised(d.clipboardByDemand), advertised(d.monitorConfig), advertised(d.monitorPosition), yes(d.fileTransferDisabled), stateName(s->imageCompression()), s->imageCompressionStatus());
    const QString monitors = monitorDetails(s->monitors(), d.requestedMonitors, d.monitorStatus);
    QString features;
    for (const auto &c : d.channels) {
        static const QStringList states = {QStringLiteral("Created"),QStringLiteral("Open"),QStringLiteral("Switching"),QStringLiteral("Closed"),QStringLiteral("Error")};
        features += QStringLiteral("%1 (type %2), ID %3: %4 %5\n").arg(c.name).arg(c.type).arg(c.id).arg(states.value(int(c.state), QStringLiteral("Unknown")), c.event);
    }
    if (d.channels.isEmpty()) features = QStringLiteral("No channels available\n");
    int connected = 0, active = 0;
    for (const auto &u : s->usbDevices()) connected += u.state == QtSpice::UsbDeviceInfo::State::Connected;
    for (const auto &t : s->fileTransfers()) active += t.state == QtSpice::FileTransferInfo::State::Queued || t.state == QtSpice::FileTransferInfo::State::Sending || t.state == QtSpice::FileTransferInfo::State::Cancelling;
    features += QStringLiteral("\nClipboard direction: %1\nClipboard ready: %2\nPlayback: %3\nMicrophone: %4\nUSB connected: %5; free channels: %6\nFile transfer ready: %7; active: %8\nShared folder: %9\nCD/DVD: %10")
        .arg(stateName(s->clipboardMode()), yes(s->clipboardReady()), stateName(s->audioState()), stateName(s->microphoneState())).arg(connected).arg(s->usbFreeChannels()).arg(yes(s->fileTransferReady())).arg(active).arg(stateName(s->sharingState()), stateName(s->sharedCdState()));
    return {overview, monitors, features};
}
inline QString report(QtSpice::Session *s) {
    const auto parts = sections(s);
    return QStringLiteral("Session Details\n\nOverview\n%1\n\nMonitors\n%2\n\nChannels & Features\n%3\n").arg(parts[0], parts[1], parts[2]);
}
inline void add(QMainWindow *window, QtSpice::Session *session, const QString &suffix = QStringLiteral("QtSpice Console")) {
    auto title = [window, session, suffix] {
        QString name = session->guestName();
        // Keep server-provided control characters out of window decorations.
        for (auto &c : name) if (!c.isPrint()) c = QChar(' ');
        window->setWindowTitle(name.isEmpty() ? suffix : name + QStringLiteral(" — ") + suffix);
    };
    title();
    QObject::connect(session, &QtSpice::Session::guestInfoChanged, window, title);
    auto *action = CompressionMenu::connectionMenu(window->menuBar())->addAction(QStringLiteral("Session Details…"));
    action->setObjectName(QStringLiteral("guestDetailsAction"));
    QObject::connect(action, &QAction::triggered, window, [window, session] {
        for (auto *widget : window->findChildren<QtSpice::Widget *>()) widget->releaseInputCapture();
        if (auto *existing = window->findChild<QDialog *>(QStringLiteral("guestDetailsDialog"))) {
            existing->show(); existing->raise(); existing->activateWindow(); return;
        }
        auto *dialog = new QDialog(window);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setObjectName(QStringLiteral("guestDetailsDialog"));
        dialog->setWindowTitle(QStringLiteral("Session Details"));
        auto *layout = new QFormLayout(dialog);
        auto *name = new QLabel(dialog); auto *uuid = new QLabel(dialog);
        name->setObjectName(QStringLiteral("guestName")); uuid->setObjectName(QStringLiteral("guestUuid"));
        for (auto *label : {name, uuid}) { label->setTextFormat(Qt::PlainText); label->setTextInteractionFlags(Qt::TextSelectableByMouse); }
        layout->addRow(QStringLiteral("Name:"), name); layout->addRow(QStringLiteral("UUID:"), uuid);
        auto refresh = [session, name, uuid] {
            name->setText(session->guestName().isEmpty() ? QStringLiteral("Not available") : session->guestName());
            uuid->setText(session->guestUuid().isEmpty() ? QStringLiteral("Not available") : session->guestUuid());
        };
        refresh(); QObject::connect(session, &QtSpice::Session::guestInfoChanged, dialog, refresh);
        // Keep identity labels for accessibility; details share one report model.
        auto *tabs = new QTabWidget(dialog);
        QList<QPlainTextEdit *> pages;
        for (const auto &title : {QStringLiteral("Overview"), QStringLiteral("Monitors"), QStringLiteral("Channels && Features")}) {
            auto *page = new QPlainTextEdit(tabs); page->setReadOnly(true);
            tabs->addTab(page, title); pages.append(page);
        }
        layout->addRow(tabs);
        auto update = [session, pages] {
            const auto parts = sections(session);
            for (int i = 0; i < pages.size(); ++i) {
                auto *page = pages[i];
                if (page->toPlainText() == parts[i]) continue;
                int scroll = page->verticalScrollBar()->value();
                auto cursor = page->textCursor(); int anchor = cursor.anchor(), position = cursor.position();
                page->setPlainText(parts[i]);
                cursor = page->textCursor(); cursor.setPosition(qMin(anchor, parts[i].size())); cursor.setPosition(qMin(position, parts[i].size()), QTextCursor::KeepAnchor); page->setTextCursor(cursor);
                page->verticalScrollBar()->setValue(scroll);
            }
        };
        update();
        QObject::connect(session, &QtSpice::Session::diagnosticsChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::guestInfoChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::stateChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::monitorsChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::inputReadyChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::mouseModeChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::clipboardModeChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::clipboardReadyChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::audioChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::microphoneChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::usbChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::sharedCdChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::sharingChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::fileTransfersChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::fileTransferReadinessChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::imageCompressionStatusChanged, dialog, update);
        QObject::connect(session, &QtSpice::Session::imageCompressionChanged, dialog, update);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        auto *copy = buttons->addButton(QStringLiteral("Copy Details"), QDialogButtonBox::ActionRole);
        copy->setObjectName(QStringLiteral("copyDetails"));
        QObject::connect(copy, &QPushButton::clicked, dialog, [session] { QApplication::clipboard()->setText(report(session)); });
        layout->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
        dialog->resize(720, 560);
        dialog->show();
    });
}
}
